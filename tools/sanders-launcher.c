/*
 * sanders-launcher.c — Android Mobile Shell & Launcher em GTK4
 * Projeto sanders-linux-mainline (Moto G5 Plus "potter", Arch ARM64, Weston 15, Adreno 506)
 *
 * Ambiente Móvel estilo Android nativo para display 1080x1920 portrait:
 *   1. Barra de Status Superior estilo Android (Relógio, Wi-Fi com SSID, Bluetooth, Som, Temperatura)
 *   2. Sistema de Páginas (GtkStack) com transição vertical/horizontal:
 *      - Página "home": Widget At-a-Glance, grade de apps rápidos, dock inferior
 *      - Página "drawer": GAVETA DINÂMICA com todos os aplicativos do sistema,
 *        descobertos automaticamente via parser de arquivos .desktop
 *        (/usr/share/applications + /usr/local/share/applications), ordenados
 *        alfabeticamente, com BUSCA em tempo real.
 *      - Página "shade": Painel Quick Settings deslizante com tiles táteis e sliders
 *      - Página "lock": Tela de bloqueio estilo Android (Ambient Display & Sleep)
 *   3. Teclado Virtual Touch Nativo GTK4 (OSK): layout QWERTY + camada
 *      numérica/simbólica, Shift, Backspace, Espaço e Enter, que sobe
 *      automaticamente quando o campo de busca recebe foco (Opção B do plano).
 *   4. Barra de Navegação Inferior estilo Android 3-Button (◀ Voltar, ⏺ Home, ⏹ Recentes/Shade)
 *   5. Controles em tempo real integrados (Brilho sysfs, Volume ALSA digital, rfkill Wi-Fi/BT)
 *
 * Compilação no device:
 *   gcc -O2 -Wall -o /usr/local/bin/sanders-launcher \
 *       tools/sanders-launcher.c $(pkg-config --cflags --libs gtk4)
 */

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/statvfs.h>

/* ------------------------------------------------------------------ */
/* Utilitários de execução e telemetria                              */
/* ------------------------------------------------------------------ */

static void launch_cmd_async(const char *cmd)
{
    GError *err = NULL;
    char *argv_cmd[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    if (!g_spawn_async(NULL, argv_cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err)) {
        g_warning("Falha ao executar '%s': %s", cmd, err ? err->message : "erro desconhecido");
        if (err) g_error_free(err);
    }
}

static char *run_get_first_line(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return NULL;
    static char buf[256];
    buf[0] = '\0';
    if (fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\r\n")] = '\0';
    }
    pclose(fp);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Backlight (Brilho da Tela via sysfs)                               */
/* ------------------------------------------------------------------ */

static char bl_path[128] = "";
static int bl_max = 4095;
static double bl_saved_before_sleep = 0.5;

static void backlight_init(void)
{
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(bl_path, sizeof(bl_path), "/sys/class/backlight/%s", e->d_name);
        break;
    }
    closedir(d);
    if (!bl_path[0]) return;

    char p[256];
    snprintf(p, sizeof(p), "%s/max_brightness", bl_path);
    FILE *f = fopen(p, "r");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1)
            bl_max = v;
        fclose(f);
    }
}

static double backlight_get(void)
{
    if (!bl_path[0]) return 0.5;
    char p[256];
    snprintf(p, sizeof(p), "%s/brightness", bl_path);
    FILE *f = fopen(p, "r");
    if (!f) return 0.5;
    int v = 0;
    if (fscanf(f, "%d", &v) != 1) v = 0;
    fclose(f);
    return (double)v / (double)bl_max;
}

static void backlight_set(double frac)
{
    if (!bl_path[0]) return;
    int v = (int)(frac * (double)bl_max);
    if (v < 1 && frac > 0.001) v = 1;
    char p[256];
    snprintf(p, sizeof(p), "%s/brightness", bl_path);
    FILE *f = fopen(p, "w");
    if (f) {
        fprintf(f, "%d", v);
        fclose(f);
    }
}

/* ------------------------------------------------------------------ */
/* Volume do Áudio (ALSA digital gains QDSP6)                         */
/* ------------------------------------------------------------------ */

static double volume_get(void)
{
    char *res = run_get_first_line("amixer -c 0 sget 'RX1 Digital' 2>/dev/null | grep -oE '\\[[0-9]+%\\]' | head -n1 | tr -d '[]%'");
    if (res && res[0])
        return atoi(res) / 100.0;
    return 0.75;
}

static void volume_set(double frac)
{
    int pct = (int)(frac * 100.0 + 0.5);
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "amixer -c 0 sset 'RX1 Digital' %d%% >/dev/null 2>&1; "
             "amixer -c 0 sset 'RX2 Digital' %d%% >/dev/null 2>&1", pct, pct);
    launch_cmd_async(cmd);
}

/* ------------------------------------------------------------------ */
/* Sensores RFKill e Térmico                                          */
/* ------------------------------------------------------------------ */

static int get_thermal_temp(void)
{
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return -1;
    int t = 0;
    if (fscanf(f, "%d", &t) != 1) t = -1000;
    fclose(f);
    return t / 1000;
}

static int rfkill_state(const char *name)
{
    DIR *d = opendir("/sys/class/rfkill");
    if (!d) return -1;
    struct dirent *e;
    int state = -1;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char npath[256], nbuf[128];
        snprintf(npath, sizeof(npath), "/sys/class/rfkill/%s/name", e->d_name);
        FILE *fn = fopen(npath, "r");
        if (!fn) continue;
        if (fgets(nbuf, sizeof(nbuf), fn)) {
            nbuf[strcspn(nbuf, "\r\n")] = '\0';
            if (strcmp(nbuf, name) == 0) {
                char spath[256];
                snprintf(spath, sizeof(spath), "/sys/class/rfkill/%s/soft", e->d_name);
                FILE *fs = fopen(spath, "r");
                if (fs) {
                    int c = fgetc(fs);
                    fclose(fs);
                    state = (c == '0') ? 1 : 0;
                }
            }
        }
        fclose(fn);
        if (state != -1) break;
    }
    closedir(d);
    return state;
}

static void rfkill_set(const char *name, int on)
{
    DIR *d = opendir("/sys/class/rfkill");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char npath[256], nbuf[128];
        snprintf(npath, sizeof(npath), "/sys/class/rfkill/%s/name", e->d_name);
        FILE *fn = fopen(npath, "r");
        if (!fn) continue;
        if (fgets(nbuf, sizeof(nbuf), fn)) {
            nbuf[strcspn(nbuf, "\r\n")] = '\0';
            if (strcmp(nbuf, name) == 0) {
                char spath[256];
                snprintf(spath, sizeof(spath), "/sys/class/rfkill/%s/soft", e->d_name);
                FILE *fs = fopen(spath, "w");
                if (fs) {
                    fputc(on ? '0' : '1', fs);
                    fclose(fs);
                }
            }
        }
        fclose(fn);
    }
    closedir(d);
}

static char *get_wifi_ssid(void)
{
    char *res = run_get_first_line("wpa_cli -i wlan0 status 2>/dev/null | grep '^ssid=' | cut -d= -f2");
    if (res && res[0])
        return res;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Estado e Componentes da Aplicação                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    GtkWidget *window;
    GtkWidget *stack;

    /* Barra de Status */
    GtkWidget *lbl_status_time;
    GtkWidget *lbl_status_wifi;
    GtkWidget *lbl_status_bt;
    GtkWidget *lbl_status_vol;
    GtkWidget *lbl_status_temp;

    /* Widget At-a-Glance Home */
    GtkWidget *lbl_widget_time;
    GtkWidget *lbl_widget_date;
    GtkWidget *lbl_widget_chip;

    /* Quick Settings Shade */
    GtkWidget *btn_shade_wifi;
    GtkWidget *lbl_shade_wifi_sub;
    GtkWidget *btn_shade_bt;
    GtkWidget *lbl_shade_bt_sub;
    GtkWidget *btn_shade_air;
    GtkWidget *btn_shade_sleep;
    GtkWidget *scale_shade_bl;
    GtkWidget *scale_shade_vol;

    /* Lock Screen */
    GtkWidget *lbl_lock_time;
    GtkWidget *lbl_lock_date;
    GtkWidget *lbl_lock_status;

    /* Drawer (Fase 2) */
    GtkWidget *dr_search;    /* GtkSearchEntry */
    GtkWidget *dr_scroll;    /* GtkScrolledWindow com a grade dinâmica */
    GtkWidget *dr_empty;     /* Label "nenhum resultado" */
    GtkWidget *lbl_dr_count; /* Contador de apps no header */

    gboolean updating_ui;

    /* Estado da busca */
    gpointer first_result;
    int n_results;
} LauncherApp;

static LauncherApp app;

/* Forward declarations (ordem de uso antes da definição) */
static GtkWidget *create_app_tile(const char *icon_glyph, const char *name,
                                  const char *desc, const char *accent_class,
                                  GCallback cb, gpointer user_data);
static void on_search_activate(GtkWidget *w, gpointer d);
static gboolean focus_search_cb(gpointer data);
static int app_cmp_name(const void *pa, const void *pb);

/* ------------------------------------------------------------------ */
/* Navegação do Stack de Telas                                        */
/* ------------------------------------------------------------------ */

static void osk_hide(void);

static void switch_to_page(const char *name)
{
    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), name);
    /* Ao sair da gaveta, fecha o teclado e limpa a busca (estado limpo). */
    if (strcmp(name, "drawer") != 0) {
        osk_hide();
        if (GTK_IS_EDITABLE(app.dr_search)) {
            const char *cur = gtk_editable_get_text(GTK_EDITABLE(app.dr_search));
            if (cur && cur[0])
                gtk_editable_set_text(GTK_EDITABLE(app.dr_search), "");
        }
    }
}

static const char *get_current_page(void)
{
    return gtk_stack_get_visible_child_name(GTK_STACK(app.stack));
}

/* ------------------------------------------------------------------ */
/* Atualização Periódica de Telemetria e Relógio                      */
/* ------------------------------------------------------------------ */

static void update_clock(void)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M", t);
    gtk_label_set_text(GTK_LABEL(app.lbl_status_time), time_str);

    /* Relógio gigante no widget Home */
    char big_time[128];
    snprintf(big_time, sizeof(big_time), "<span font='84' weight='bold' letter_spacing='-1000'>%s</span>", time_str);
    gtk_label_set_markup(GTK_LABEL(app.lbl_widget_time), big_time);

    /* Relógio na Lock Screen */
    char lock_time[128];
    snprintf(lock_time, sizeof(lock_time), "<span font='96' weight='bold' letter_spacing='-1500'>%s</span>", time_str);
    gtk_label_set_markup(GTK_LABEL(app.lbl_lock_time), lock_time);

    /* Data formatada em português */
    static const char *dias[] = { "Domingo", "Segunda-feira", "Terça-feira", "Quarta-feira",
                                  "Quinta-feira", "Sexta-feira", "Sábado" };
    static const char *meses[] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho",
                                   "julho", "agosto", "setembro", "outubro", "novembro", "dezembro" };
    char date_str[256];
    snprintf(date_str, sizeof(date_str),
             "<span font='18' weight='500' alpha='85%%'>%s, %d de %s</span>",
             dias[t->tm_wday], t->tm_mday, meses[t->tm_mon]);
    gtk_label_set_markup(GTK_LABEL(app.lbl_widget_date), date_str);
    gtk_label_set_markup(GTK_LABEL(app.lbl_lock_date), date_str);
}

static void set_tile_active_class(GtkWidget *btn, gboolean active)
{
    if (active)
        gtk_widget_add_css_class(btn, "tile-active");
    else
        gtk_widget_remove_css_class(btn, "tile-active");
}

static void update_telemetry(void)
{
    app.updating_ui = TRUE;

    /* Wi-Fi */
    int wf = rfkill_state("phy0");
    if (wf == 1) {
        char *ssid = get_wifi_ssid();
        if (ssid) {
            char txt[64];
            snprintf(txt, sizeof(txt), "📶 %s", ssid);
            gtk_label_set_text(GTK_LABEL(app.lbl_status_wifi), txt);
            gtk_label_set_text(GTK_LABEL(app.lbl_shade_wifi_sub), ssid);
        } else {
            gtk_label_set_text(GTK_LABEL(app.lbl_status_wifi), "📶 Conectando…");
            gtk_label_set_text(GTK_LABEL(app.lbl_shade_wifi_sub), "Buscando…");
        }
        set_tile_active_class(app.btn_shade_wifi, TRUE);
    } else {
        gtk_label_set_text(GTK_LABEL(app.lbl_status_wifi), "📶 Off");
        gtk_label_set_text(GTK_LABEL(app.lbl_shade_wifi_sub), "Desligado");
        set_tile_active_class(app.btn_shade_wifi, FALSE);
    }

    /* Bluetooth */
    int bt = rfkill_state("hci0");
    gtk_label_set_text(GTK_LABEL(app.lbl_status_bt), (bt == 1) ? "ᛒ On" : "ᛒ Off");
    gtk_label_set_text(GTK_LABEL(app.lbl_shade_bt_sub), (bt == 1) ? "Ativo" : "Desligado");
    set_tile_active_class(app.btn_shade_bt, bt == 1);

    /* Avião */
    set_tile_active_class(app.btn_shade_air, wf == 0 && bt == 0);

    /* Volume */
    double vol = volume_get();
    char vstr[32];
    snprintf(vstr, sizeof(vstr), "🔊 %d%%", (int)(vol * 100 + 0.5));
    gtk_label_set_text(GTK_LABEL(app.lbl_status_vol), vstr);
    if (GTK_IS_RANGE(app.scale_shade_vol))
        gtk_range_set_value(GTK_RANGE(app.scale_shade_vol), vol);

    /* Brilho */
    double bl = backlight_get();
    if (GTK_IS_RANGE(app.scale_shade_bl))
        gtk_range_set_value(GTK_RANGE(app.scale_shade_bl), bl);

    /* Temperatura */
    int temp = get_thermal_temp();
    if (temp > 0) {
        char tstr[32];
        snprintf(tstr, sizeof(tstr), "🌡️ %d°C", temp);
        gtk_label_set_text(GTK_LABEL(app.lbl_status_temp), tstr);
    }

    app.updating_ui = FALSE;
}

static gboolean on_second_timer(gpointer data G_GNUC_UNUSED)
{
    update_clock();
    return G_SOURCE_CONTINUE;
}

static gboolean on_telemetry_timer(gpointer data G_GNUC_UNUSED)
{
    update_telemetry();
    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Callbacks dos Controles do Quick Settings Shade                    */
/* ------------------------------------------------------------------ */

static void on_toggle_wifi(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    int curr = rfkill_state("phy0");
    rfkill_set("phy0", curr != 1);
    update_telemetry();
}

static void on_toggle_bt(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    int curr = rfkill_state("hci0");
    rfkill_set("hci0", curr != 1);
    update_telemetry();
}

static void on_toggle_airplane(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    int wf = rfkill_state("phy0");
    int bt = rfkill_state("hci0");
    int to_airplane = !(wf == 0 && bt == 0);
    rfkill_set("phy0", !to_airplane);
    rfkill_set("hci0", !to_airplane);
    update_telemetry();
}

static void on_shade_bl_changed(GtkRange *r, gpointer d G_GNUC_UNUSED)
{
    if (!app.updating_ui)
        backlight_set(gtk_range_get_value(r));
}

static void on_shade_vol_changed(GtkRange *r, gpointer d G_GNUC_UNUSED)
{
    if (!app.updating_ui)
        volume_set(gtk_range_get_value(r));
}

/* ------------------------------------------------------------------ */
/* Sleep Mode & Tela de Bloqueio                                      */
/* ------------------------------------------------------------------ */

static void enter_sleep_lock(void)
{
    bl_saved_before_sleep = backlight_get();
    if (bl_saved_before_sleep < 0.1) bl_saved_before_sleep = 0.5;
    backlight_set(0.0);
    switch_to_page("lock");
}

static void unlock_screen(void)
{
    backlight_set(bl_saved_before_sleep);
    switch_to_page("home");
}

/* ------------------------------------------------------------------ */
/* Ações de Aplicativos Fixos e Diálogos                              */
/* ------------------------------------------------------------------ */

static void on_open_music(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    launch_cmd_async("/root/mp3_player/player_gtk4 &");
}

static void on_open_terminal(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    launch_cmd_async("weston-terminal &");
}

static void on_open_htop(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    launch_cmd_async("weston-terminal --shell=/usr/bin/htop &");
}

static void on_open_fastfetch(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    launch_cmd_async("weston-terminal --shell='bash -c \"fastfetch; exec bash\"' &");
}

static void act_music(void)    { on_open_music(NULL, NULL); }

static void show_system_info_dialog(GtkWidget *parent)
{
    GtkWidget *win = gtk_window_new();
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(parent));
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_title(GTK_WINDOW(win), "Sobre o Dispositivo");
    gtk_window_set_default_size(GTK_WINDOW(win), 500, 680);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span font='22' weight='bold' color='#E3E3E3'>📱 Motorola Moto G5 Plus</span>\n"
        "<span font='14' color='#9AA0A6'>Arch Linux ARM64 • Snapdragon 625</span>");
    gtk_label_set_justify(GTK_LABEL(title), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(box), title);

    char *kver = run_get_first_line("uname -r");
    char *uptime = run_get_first_line("uptime -p");
    char *ip_wlan = run_get_first_line("ip -4 addr show wlan0 2>/dev/null | grep -oP '(?<=inet\\s)\\d+(\\.\\d+){3}'");
    char *ip_usb = run_get_first_line("ip -4 addr show usb0 2>/dev/null | grep -oP '(?<=inet\\s)\\d+(\\.\\d+){3}'");

    char info[2048];
    snprintf(info, sizeof(info),
        "<span weight='bold'>SoC:</span> Qualcomm MSM8953 (Snapdragon 625)\n"
        "<span weight='bold'>CPU:</span> 8x ARM Cortex-A53 @ 2.0 GHz\n"
        "<span weight='bold'>GPU:</span> Adreno 506 (freedreno OpenGL acel.)\n"
        "<span weight='bold'>Display:</span> 1080x1920 Full HD Nativo (DSI-1)\n"
        "<span weight='bold'>Touch:</span> Synaptics S3603R Multi-Touch\n"
        "<span weight='bold'>Kernel:</span> %s (Mainline Linux)\n"
        "<span weight='bold'>Bootloader:</span> lk2nd 2nd-stage via ABOOT\n"
        "<span weight='bold'>Wi-Fi IP:</span> %s\n"
        "<span weight='bold'>USB IP:</span> %s\n"
        "<span weight='bold'>Áudio:</span> Hexagon QDSP6 + PM8953 WCD\n"
        "<span weight='bold'>Atividade:</span> %s",
        kver ? kver : "Linux 7.2",
        ip_wlan ? ip_wlan : "Desconectado",
        ip_usb ? ip_usb : "10.42.0.2",
        uptime ? uptime : "Desconhecido");

    GtkWidget *lbl_info = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_info), info);
    gtk_label_set_xalign(GTK_LABEL(lbl_info), 0.0);
    gtk_widget_add_css_class(lbl_info, "info-card");
    gtk_box_append(GTK_BOX(box), lbl_info);

    GtkWidget *btn_close = gtk_button_new_with_label("Fechar");
    gtk_widget_set_size_request(btn_close, -1, 52);
    gtk_widget_add_css_class(btn_close, "btn-action");
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_window_destroy), win);
    gtk_box_append(GTK_BOX(box), btn_close);

    gtk_window_present(GTK_WINDOW(win));
}

static void show_storage_dialog(GtkWidget *parent)
{
    GtkWidget *win = gtk_window_new();
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(parent));
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_title(GTK_WINDOW(win), "Armazenamento & Disco");
    gtk_window_set_default_size(GTK_WINDOW(win), 500, 580);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span font='22' weight='bold' color='#E3E3E3'>📁 Armazenamento & eMMC</span>\n"
        "<span font='14' color='#9AA0A6'>Partições e Discos Locais</span>");
    gtk_label_set_justify(GTK_LABEL(title), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(box), title);

    struct statvfs sv;
    char details[2048] = "";
    if (statvfs("/", &sv) == 0) {
        double total_gb = (double)(sv.f_blocks * sv.f_frsize) / (1024.0 * 1024.0 * 1024.0);
        double free_gb = (double)(sv.f_bavail * sv.f_frsize) / (1024.0 * 1024.0 * 1024.0);
        double used_gb = total_gb - free_gb;
        snprintf(details, sizeof(details),
            "<span font='16' weight='bold'>💾 Memória Flash eMMC (/)</span>\n"
            "• Usado: <b>%.2f GB</b> / Total: <b>%.2f GB</b> (Livre: <b>%.2f GB</b>)\n\n"
            "<span font='16' weight='bold'>⚡ ZRAM Swap Comprimido</span>\n"
            "• Ativo: <b>1.4 GB</b> em RAM (algoritmo zstd/lz4)\n\n"
            "<span font='16' weight='bold'>📦 Partição de Boot (/cache)</span>\n"
            "• Partição ext2 dedicada para lk2nd e extlinux (256 MB)\n\n"
            "<span font='16' weight='bold'>🗂️ Cartão MicroSD</span>\n"
            "• Montagem automática sob demanda em <b>/mnt/microsd</b>",
            used_gb, total_gb, free_gb);
    } else {
        snprintf(details, sizeof(details), "Erro ao consultar statvfs.");
    }

    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl), details);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_add_css_class(lbl, "info-card");
    gtk_box_append(GTK_BOX(box), lbl);

    GtkWidget *btn_close = gtk_button_new_with_label("Fechar");
    gtk_widget_set_size_request(btn_close, -1, 52);
    gtk_widget_add_css_class(btn_close, "btn-action");
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_window_destroy), win);
    gtk_box_append(GTK_BOX(box), btn_close);

    gtk_window_present(GTK_WINDOW(win));
}

static void show_power_dialog(GtkWidget *parent)
{
    GtkWidget *win = gtk_window_new();
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(parent));
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_title(GTK_WINDOW(win), "Menu de Energia");
    gtk_window_set_default_size(GTK_WINDOW(win), 460, 480);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span font='22' weight='bold' color='#E3E3E3'>⏻ Opções de Energia</span>\n"
        "<span font='14' color='#9AA0A6'>Selecione uma ação</span>");
    gtk_label_set_justify(GTK_LABEL(title), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget *btn_reb = gtk_button_new_with_label("⟳ Reiniciar Sistema");
    gtk_widget_set_size_request(btn_reb, -1, 60);
    gtk_widget_add_css_class(btn_reb, "btn-power-reboot");
    g_signal_connect_swapped(btn_reb, "clicked", G_CALLBACK(launch_cmd_async), "systemctl reboot");
    gtk_box_append(GTK_BOX(box), btn_reb);

    GtkWidget *btn_off = gtk_button_new_with_label("⏻ Desligar Dispositivo");
    gtk_widget_set_size_request(btn_off, -1, 60);
    gtk_widget_add_css_class(btn_off, "btn-power-off");
    g_signal_connect_swapped(btn_off, "clicked", G_CALLBACK(launch_cmd_async), "systemctl poweroff");
    gtk_box_append(GTK_BOX(box), btn_off);

    GtkWidget *btn_close = gtk_button_new_with_label("Cancelar");
    gtk_widget_set_size_request(btn_close, -1, 50);
    gtk_widget_add_css_class(btn_close, "btn-action");
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_window_destroy), win);
    gtk_box_append(GTK_BOX(box), btn_close);

    gtk_window_present(GTK_WINDOW(win));
}

/* Wrappers sem argumentos para a gaveta dinâmica (Fase 2) */
static void act_htop(void)      { on_open_htop(NULL, NULL); }
static void act_fastfetch(void) { on_open_fastfetch(NULL, NULL); }
static void act_storage(void)   { show_storage_dialog(NULL); }
static void act_about(void)     { show_system_info_dialog(NULL); }
static void act_power(void)     { show_power_dialog(NULL); }

/* ------------------------------------------------------------------ */
/* FASE 2 — Parser de Arquivos .desktop (Descoberta de Apps)          */
/* ------------------------------------------------------------------ */

#define APPS_MAX 64

typedef struct {
    char *name;        /* Nome exibido (Name[pt_BR] > Name) */
    char *desc;        /* Subtítulo (primeiro token do Exec) */
    char *key;         /* Nome normalizado p/ busca (lowercase, sem acentos) */
    char *desktop_id;  /* ID .desktop p/ gtk-launch */
    char *exec_cmd;    /* Linha Exec crua (p/ apps de terminal) */
    GCallback func;    /* Callback nativo (ferramentas do shell) */
    char glyph[12];    /* Ícone emoji */
    const char *accent;/* Classe CSS de cor */
    char tooltip[512];
    gboolean terminal; /* Exec pede terminal (Terminal=true) */
} DesktopApp;

static DesktopApp apps[APPS_MAX];
static int n_apps = 0;

/* Normaliza p/ busca: casefold + NFKD + remove marcas de combinação (acentos) */
static char *normalize_key(const char *s)
{
    char *cf = g_utf8_casefold(s, -1);
    char *nf = g_utf8_normalize(cf, -1, G_NORMALIZE_NFKD);
    g_free(cf);
    if (!nf)
        return g_strdup(s);
    GString *gs = g_string_new(NULL);
    for (char *p = nf; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_ismark(c))
            continue; /* remove marcas de combinação (acentos) */
        g_string_append_unichar(gs, c);
    }
    g_free(nf);
    return g_string_free(gs, FALSE);
}

/* Retorna ponteiro para o valor de "Chave=valor" ou NULL */
static const char *key_value(const char *line, const char *key)
{
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0)
        return NULL;
    if (line[klen] != '=')
        return NULL;
    return line + klen + 1;
}

/* Heurística de ícone emoji pelo nome do app */
static const char *pick_glyph(const char *lname)
{
    if (strstr(lname, "terminal") || strstr(lname, "console")) return "💻";
    if (strstr(lname, "arquiv") || strstr(lname, "file") || strstr(lname, "storage") ||
        strstr(lname, "armazen") || strstr(lname, "disk") || strstr(lname, "nautilus")) return "📁";
    if (strstr(lname, "web") || strstr(lname, "browser") || strstr(lname, "naveg") ||
        strstr(lname, "firefox") || strstr(lname, "chrom") || strstr(lname, "epiphany")) return "🌐";
    if (strstr(lname, "music") || strstr(lname, "player") || strstr(lname, "mp3") ||
        strstr(lname, "audio") || strstr(lname, "som") || strstr(lname, "video")) return "🎵";
    if (strstr(lname, "config") || strstr(lname, "settings") || strstr(lname, "ajustes") ||
        strstr(lname, "prefer")) return "⚙️";
    if (strstr(lname, "calc")) return "🧮";
    if (strstr(lname, "text") || strstr(lname, "editor") || strstr(lname, "vim") ||
        strstr(lname, "nano") || strstr(lname, "gedit")) return "📝";
    if (strstr(lname, "htop") || strstr(lname, "monitor") || strstr(lname, "process") ||
        strstr(lname, "task")) return "📊";
    if (strstr(lname, "camera") || strstr(lname, "foto")) return "📷";
    if (strstr(lname, "paint") || strstr(lname, "image") || strstr(lname, "gimp") ||
        strstr(lname, "draw") || strstr(lname, "inkscape")) return "🎨";
    if (strstr(lname, "mail") || strstr(lname, "email")) return "✉️";
    if (strstr(lname, "map")) return "🗺️";
    if (strstr(lname, "clock") || strstr(lname, "timer") || strstr(lname, "hora")) return "⏰";
    if (strstr(lname, "energ") || strstr(lname, "power") || strstr(lname, "sistema") ||
        strstr(lname, "sobre")) return "⏻";
    if (strstr(lname, "fastfetch") || strstr(lname, "info")) return "🚀";
    return "📦";
}

static const char *pick_accent(const char *name)
{
    static const char *accents[] = { "tile-music", "tile-settings", "tile-terminal",
                                     "tile-storage", "tile-info", "tile-power" };
    unsigned h = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        h = h * 31u + *p;
    return accents[h % G_N_ELEMENTS(accents)];
}

static void add_func_app(const char *name, const char *desc, const char *glyph, GCallback cb)
{
    if (n_apps >= APPS_MAX)
        return;
    DesktopApp *a = &apps[n_apps++];
    a->name = g_strdup(name);
    a->desc = g_strdup(desc);
    a->key = normalize_key(name);
    a->desktop_id = NULL;
    a->exec_cmd = NULL;
    a->func = cb;
    snprintf(a->glyph, sizeof(a->glyph), "%s", glyph);
    a->accent = pick_accent(name);
    a->terminal = FALSE;
    snprintf(a->tooltip, sizeof(a->tooltip), "%s (ferramenta do shell)", name);
}

/* Escapa " e \ para uso dentro de bash -c "..." */
static void escape_for_bash(const char *src, char *dst, size_t dstsz)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 2 < dstsz; i++) {
        if (src[i] == '"' || src[i] == '\\')
            dst[j++] = '\\';
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

static void parse_desktop_file(const char *path, const char *id)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;

    char line[512];
    gboolean inside = FALSE;
    gboolean is_app = FALSE, hidden = FALSE, nodisp = FALSE, terminal = FALSE;
    char name[256] = "", name_l10n[256] = "", exec[384] = "", tryexec[256] = "";

    while (fgets(line, sizeof(line), f)) {
        /* trim: início */
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        s[strcspn(s, "\r\n")] = '\0';
        if (!s[0] || s[0] == '#')
            continue;
        if (s[0] == '[') {
            inside = (strcmp(s, "[Desktop Entry]") == 0);
            continue;
        }
        if (!inside)
            continue;

        const char *v;
        if ((v = key_value(s, "Type"))) {
            is_app = (strcmp(v, "Application") == 0);
        } else if ((v = key_value(s, "Hidden"))) {
            hidden = (strcmp(v, "true") == 0);
        } else if ((v = key_value(s, "NoDisplay"))) {
            nodisp = (strcmp(v, "true") == 0);
        } else if ((v = key_value(s, "Terminal"))) {
            terminal = (strcmp(v, "true") == 0);
        } else if ((v = key_value(s, "Name[pt_BR]"))) {
            snprintf(name_l10n, sizeof(name_l10n), "%s", v);
        } else if ((v = key_value(s, "Name[pt]")) && !name_l10n[0]) {
            snprintf(name_l10n, sizeof(name_l10n), "%s", v);
        } else if ((v = key_value(s, "Name")) && !name[0]) {
            snprintf(name, sizeof(name), "%s", v);
        } else if ((v = key_value(s, "Exec")) && !exec[0]) {
            snprintf(exec, sizeof(exec), "%s", v);
        } else if ((v = key_value(s, "TryExec")) && !tryexec[0]) {
            snprintf(tryexec, sizeof(tryexec), "%s", v);
        }
    }
    fclose(f);

    if (!is_app || hidden || nodisp || !exec[0])
        return;
    /* Campos %f %F %u %U %i %c — removidos (sem documento aberto) */
    char *pct = strchr(exec, '%');
    if (pct)
        *pct = '\0';
    g_strchomp(exec);
    if (!exec[0])
        return;
    /* TryExec: pular se o binário não existir/exequível */
    if (tryexec[0] && access(tryexec, X_OK) != 0)
        return;

    if (n_apps >= APPS_MAX)
        return;

    DesktopApp *a = &apps[n_apps++];
    a->name = g_strdup(name_l10n[0] ? name_l10n : (name[0] ? name : id));
    a->key = normalize_key(a->name);
    a->desktop_id = g_strdup(id);
    a->exec_cmd = g_strdup(exec);
    a->func = NULL;
    a->terminal = terminal;
    a->accent = pick_accent(a->name);
    snprintf(a->glyph, sizeof(a->glyph), "%s", pick_glyph(a->key));

    /* Desc: primeiro token do Exec (binário) */
    char first_tok[128] = "";
    sscanf(exec, "%127s", first_tok);
    char *base = strrchr(first_tok, '/');
    a->desc = g_strdup(base ? base + 1 : first_tok);

    /* Tooltip com a linha Exec crua */
    snprintf(a->tooltip, sizeof(a->tooltip), "Exec: %s", exec);
}

static void load_applications(void)
{
    n_apps = 0;
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    static const char *dirs[] = {
        "/usr/local/share/applications", /* prioridade */
        "/usr/share/applications",
    };

    for (size_t di = 0; di < G_N_ELEMENTS(dirs); di++) {
        GDir *d = g_dir_open(dirs[di], 0, NULL);
        if (!d)
            continue;
        const char *fn;
        while ((fn = g_dir_read_name(d))) {
            if (!g_str_has_suffix(fn, ".desktop"))
                continue;
            char *id = g_strdup(fn);
            char *slash = strchr(id, '/');
            if (slash)
                *slash = '-'; /* ID XDG: subdiretórios viram hífen */
            if (g_hash_table_contains(seen, id)) {
                g_free(id);
                continue;
            }
            char *path = g_build_filename(dirs[di], fn, NULL);
            parse_desktop_file(path, id);
            g_free(path);
            g_hash_table_add(seen, id);
        }
        g_dir_close(d);
    }
    g_hash_table_unref(seen);

    /* Ferramentas nativas do shell (sempre disponíveis, filtráveis na busca) */
    add_func_app("Música", "Player MP3 GTK4", "🎵", G_CALLBACK(act_music));
    add_func_app("Processos (Htop)", "Monitor de processos", "📊", G_CALLBACK(act_htop));
    add_func_app("Fastfetch", "Specs de hardware", "🚀", G_CALLBACK(act_fastfetch));
    add_func_app("Sobre o Dispositivo", "Informações do sistema", "ℹ️", G_CALLBACK(act_about));
    add_func_app("Energia (Power Menu)", "Reiniciar / Desligar", "⏻", G_CALLBACK(act_power));
    add_func_app("Armazenamento & eMMC", "Partições e disco", "📁", G_CALLBACK(act_storage));

    /* Ordenação alfabética */
    qsort(apps, n_apps, sizeof(DesktopApp), app_cmp_name);
}

/* Ordenação alfabética case-insensitive via chave normalizada */
static int app_cmp_name(const void *pa, const void *pb)
{
    const DesktopApp *a = pa, *b = pb;
    return strcmp(a->key, b->key);
}

static void launch_desktop_app(DesktopApp *a)
{
    if (a->func) {
        ((void (*)(void))a->func)();
        return;
    }
    char cmd[1280];
    if (a->terminal && a->exec_cmd && a->exec_cmd[0]) {
        /* App de TUI (Terminal=true): roda dentro do weston-terminal
           (padrão já validado no device para htop/fastfetch) */
        char esc[768];
        escape_for_bash(a->exec_cmd, esc, sizeof(esc));
        snprintf(cmd, sizeof(cmd),
                 "weston-terminal --shell='bash -c \"%s; exec bash\"' &", esc);
    } else {
        snprintf(cmd, sizeof(cmd), "gtk-launch %s &", a->desktop_id);
    }
    launch_cmd_async(cmd);
}

/* ------------------------------------------------------------------ */
/* FASE 2 — Teclado Virtual Touch Nativo (OSK GTK4)                   */
/* ------------------------------------------------------------------ */

typedef struct {
    GtkWidget *reveal;        /* GtkRevealer com o teclado */
    gboolean shift;
    gboolean symbols;
    GtkWidget *btn_shift;
    GtkWidget *layer_letters; /* Caixa com as 3 fileiras de letras */
    GtkWidget *layer_symbols; /* Caixa com as 3 fileiras de símbolos */
    GtkWidget *btn_letters_toggle;
    GtkWidget *btn_symbols_toggle;
    GtkWidget *letter_btns[26];
} Osk;

static Osk osk;

static void osk_show(void)
{
    if (osk.reveal)
        gtk_revealer_set_reveal_child(GTK_REVEALER(osk.reveal), TRUE);
}

static void osk_hide(void)
{
    if (osk.reveal)
        gtk_revealer_set_reveal_child(GTK_REVEALER(osk.reveal), FALSE);
}

/* Entrada de texto no GtkSearchEntry via interface GtkEditable */
static void osk_insert_text(const char *s)
{
    if (!GTK_IS_EDITABLE(app.dr_search))
        return;
    int pos = gtk_editable_get_position(GTK_EDITABLE(app.dr_search));
    gtk_editable_insert_text(GTK_EDITABLE(app.dr_search), s, -1, &pos);
    gtk_editable_set_position(GTK_EDITABLE(app.dr_search), pos);
}

static void on_key_backspace(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    if (!GTK_IS_EDITABLE(app.dr_search))
        return;
    int pos = gtk_editable_get_position(GTK_EDITABLE(app.dr_search));
    if (pos > 0)
        gtk_editable_delete_text(GTK_EDITABLE(app.dr_search), pos - 1, pos);
}

static void on_key_letter(GtkWidget *w G_GNUC_UNUSED, gpointer d)
{
    int code = GPOINTER_TO_INT(d); /* 'a'..'z' */
    char ch = osk.shift ? (char)(code - 32) : (char)code;
    char s[2] = { ch, '\0' };
    osk_insert_text(s);
    /* Shift é one-shot, como no Android */
    if (osk.shift) {
        osk.shift = FALSE;
        if (osk.btn_shift)
            gtk_widget_remove_css_class(osk.btn_shift, "osk-shift-on");
    }
}

static void on_key_sym(GtkWidget *w G_GNUC_UNUSED, gpointer d)
{
    osk_insert_text((const char *)d);
}

static void on_key_space(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    osk_insert_text(" ");
}

static void on_key_shift(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    osk.shift = !osk.shift;
    if (osk.btn_shift) {
        if (osk.shift)
            gtk_widget_add_css_class(osk.btn_shift, "osk-shift-on");
        else
            gtk_widget_remove_css_class(osk.btn_shift, "osk-shift-on");
    }
    /* Atualiza a legenda das teclas de A..Z */
    for (int i = 0; i < 26; i++) {
        if (!osk.letter_btns[i])
            continue;
        GtkWidget *lbl = gtk_button_get_child(GTK_BUTTON(osk.letter_btns[i]));
        if (GTK_IS_LABEL(lbl)) {
            char c = osk.shift ? (char)('A' + i) : (char)('a' + i);
            char m[64];
            snprintf(m, sizeof(m), "<span font='22' weight='500'>%c</span>", c);
            gtk_label_set_markup(GTK_LABEL(lbl), m);
        }
    }
}

static void on_key_toggle_layer(GtkWidget *w G_GNUC_UNUSED, gpointer d)
{
    gboolean show_symbols = GPOINTER_TO_INT(d) != 0;
    osk.symbols = show_symbols;
    if (osk.layer_letters)
        gtk_widget_set_visible(osk.layer_letters, !show_symbols);
    if (osk.layer_symbols)
        gtk_widget_set_visible(osk.layer_symbols, show_symbols);
}

/* GClosureNotify compatível com g_free (evita cast de tipo de função) */
static void osk_sym_free_notify(gpointer data, GClosure *closure G_GNUC_UNUSED)
{
    g_free(data);
}

/* Fileira de teclas de um grid (uma linha) a partir de uma string */
static GtkWidget *make_osk_row(const char *keys, gboolean letters)
{
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);

    int col = 0;
    for (const char *p = keys; *p; p++) {
        GtkWidget *btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "osk-key");
        gtk_widget_set_focusable(btn, FALSE); /* não rouba foco do campo de busca */

        GtkWidget *lbl = gtk_label_new(NULL);
        if (letters) {
            char m[64];
            snprintf(m, sizeof(m), "<span font='22' weight='500'>%c</span>", *p);
            gtk_label_set_markup(GTK_LABEL(lbl), m);
        } else {
            char s[2] = { *p, '\0' };
            gtk_label_set_text(GTK_LABEL(lbl), s);
            gtk_widget_add_css_class(lbl, "osk-key-lbl");
        }
        gtk_button_set_child(GTK_BUTTON(btn), lbl);

        if (letters) {
            int idx = *p - 'a';
            if (idx >= 0 && idx < 26)
                osk.letter_btns[idx] = btn;
            g_signal_connect(btn, "clicked", G_CALLBACK(on_key_letter), GINT_TO_POINTER(*p));
        } else {
            char *sym = g_strndup(p, 1);
            g_signal_connect_data(btn, "clicked", G_CALLBACK(on_key_sym), sym,
                                  osk_sym_free_notify, 0);
        }

        gtk_grid_attach(GTK_GRID(grid), btn, col++, 0, 1, 1);
    }
    return grid;
}

static GtkWidget *make_osk_button(const char *label_markup, const char *css_extra,
                                  GCallback cb, gpointer data)
{
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "osk-key");
    if (css_extra)
        gtk_widget_add_css_class(btn, css_extra);
    gtk_widget_set_focusable(btn, FALSE);

    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl), label_markup);
    gtk_button_set_child(GTK_BUTTON(btn), lbl);

    if (cb)
        g_signal_connect(btn, "clicked", cb, data);
    return btn;
}

static GtkWidget *build_osk(void)
{
    GtkWidget *rev = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(rev), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_transition_duration(GTK_REVEALER(rev), 220);
    gtk_revealer_set_reveal_child(GTK_REVEALER(rev), FALSE);
    gtk_widget_set_hexpand(rev, TRUE);

    GtkWidget *kb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(kb, "osk");
    gtk_widget_set_margin_start(kb, 10);
    gtk_widget_set_margin_end(kb, 10);
    gtk_widget_set_margin_top(kb, 12);
    gtk_widget_set_margin_bottom(kb, 10);
    gtk_revealer_set_child(GTK_REVEALER(rev), kb);

    /* ------- Camada de LETRAS (QWERTY) ------- */
    GtkWidget *letters = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    osk.layer_letters = letters;

    GtkWidget *r1 = make_osk_row("qwertyuiop", TRUE);
    gtk_widget_set_halign(r1, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(letters), r1);

    GtkWidget *r2 = make_osk_row("asdfghjkl", TRUE);
    gtk_widget_set_halign(r2, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(letters), r2);

    GtkWidget *row3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(row3, GTK_ALIGN_CENTER);

    osk.btn_shift = make_osk_button("<span font='20'>⇧</span>", "osk-key-wide",
                                    G_CALLBACK(on_key_shift), NULL);
    gtk_box_append(GTK_BOX(row3), osk.btn_shift);

    GtkWidget *r3 = make_osk_row("zxcvbnm", TRUE);
    gtk_box_append(GTK_BOX(row3), r3);

    GtkWidget *bs = make_osk_button("<span font='20'>⌫</span>", "osk-key-wide",
                                    G_CALLBACK(on_key_backspace), NULL);
    gtk_box_append(GTK_BOX(row3), bs);

    gtk_box_append(GTK_BOX(letters), row3);
    gtk_box_append(GTK_BOX(kb), letters);

    /* ------- Camada NUMÉRICA/SÍMBOLOS ------- */
    GtkWidget *syms = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_visible(syms, FALSE);
    osk.layer_symbols = syms;

    GtkWidget *s1 = make_osk_row("1234567890", FALSE);
    gtk_widget_set_halign(s1, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(syms), s1);

    GtkWidget *s2 = make_osk_row("@#$%&-+()", FALSE);
    gtk_widget_set_halign(s2, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(syms), s2);

    GtkWidget *s3 = make_osk_row("=\\<>[]{}*\"'", FALSE);
    gtk_widget_set_halign(s3, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(syms), s3);

    GtkWidget *bsym = make_osk_button("<span font='20'>⌫</span>", "osk-key-wide",
                                      G_CALLBACK(on_key_backspace), NULL);
    gtk_widget_set_halign(bsym, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(syms), bsym);

    gtk_box_append(GTK_BOX(kb), syms);

    /* ------- Fileira inferior: camadas + espaço + enter ------- */
    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    osk.btn_letters_toggle = make_osk_button("<span font='18' weight='bold'>?123</span>",
                                             "osk-key-wide", G_CALLBACK(on_key_toggle_layer),
                                             GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(bottom), osk.btn_letters_toggle);

    osk.btn_symbols_toggle = make_osk_button("<span font='18' weight='bold'>ABC</span>",
                                             "osk-key-wide", G_CALLBACK(on_key_toggle_layer),
                                             GINT_TO_POINTER(0));
    gtk_widget_set_visible(osk.btn_symbols_toggle, FALSE);
    gtk_box_append(GTK_BOX(bottom), osk.btn_symbols_toggle);

    GtkWidget *space = make_osk_button("<span font='18'>espaço</span>", "osk-key-space",
                                       G_CALLBACK(on_key_space), NULL);
    gtk_widget_set_hexpand(space, TRUE);
    gtk_box_append(GTK_BOX(bottom), space);

    GtkWidget *enter = make_osk_button("<span font='20'>⏎</span>", "osk-key-accent",
                                       G_CALLBACK(on_search_activate), NULL);
    gtk_box_append(GTK_BOX(bottom), enter);

    gtk_box_append(GTK_BOX(kb), bottom);

    osk.reveal = rev;
    return rev;
}

static void on_entry_focus_changed(GObject *obj, GParamSpec *ps G_GNUC_UNUSED,
                                   gpointer d G_GNUC_UNUSED)
{
    if (gtk_widget_has_focus(GTK_WIDGET(obj)))
        osk_show();
}

static void on_osk_toggle_button(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    if (osk.reveal &&
        gtk_revealer_get_reveal_child(GTK_REVEALER(osk.reveal)))
        osk_hide();
    else
        osk_show();
}

/* ------------------------------------------------------------------ */
/* FASE 2 — Gaveta Dinâmica de Apps (grade + busca)                   */
/* ------------------------------------------------------------------ */

static void on_drawer_tile_clicked(GtkWidget *w G_GNUC_UNUSED, gpointer d)
{
    launch_desktop_app((DesktopApp *)d);
}

static void rebuild_drawer_grid(void)
{
    const char *q_raw = gtk_editable_get_text(GTK_EDITABLE(app.dr_search));
    char *q = normalize_key(q_raw);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

    int count = 0;
    app.first_result = NULL;
    for (int i = 0; i < n_apps; i++) {
        if (q[0] && !strstr(apps[i].key, q))
            continue;
        if (!app.first_result)
            app.first_result = &apps[i];
        GtkWidget *tile = create_app_tile(apps[i].glyph, apps[i].name, apps[i].desc,
                                          apps[i].accent, G_CALLBACK(on_drawer_tile_clicked),
                                          &apps[i]);
        gtk_widget_add_css_class(tile, "drawer-tile");
        gtk_widget_set_tooltip_text(tile, apps[i].tooltip);
        gtk_grid_attach(GTK_GRID(grid), tile, count % 3, count / 3, 1, 1);
        count++;
    }

    char cnt[80];
    snprintf(cnt, sizeof(cnt), "<span font='15' color='#9AA0A6'>%d apps</span>", count);
    gtk_label_set_markup(GTK_LABEL(app.lbl_dr_count), cnt);
    gtk_widget_set_visible(app.dr_empty, count == 0);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(app.dr_scroll), grid);
    app.n_results = count;
    g_free(q);
}

static void on_search_changed(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    rebuild_drawer_grid();
}

static void on_search_activate(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    if (app.first_result)
        launch_desktop_app((DesktopApp *)app.first_result);
}

static void on_open_drawer_search(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    switch_to_page("drawer");
    /* Foca o campo após a transição do stack → sobe o teclado automaticamente */
    g_timeout_add(280, focus_search_cb, NULL);
}

static gboolean focus_search_cb(gpointer data G_GNUC_UNUSED)
{
    if (GTK_IS_WIDGET(app.dr_search))
        gtk_widget_grab_focus(app.dr_search);
    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/* Ações da Barra de Navegação Android (3-Button Nav)                 */
/* ------------------------------------------------------------------ */

static void on_nav_back(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    const char *curr = get_current_page();
    if (strcmp(curr, "home") != 0) {
        switch_to_page("home");
    }
}

static void on_nav_home(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    switch_to_page("home");
}

static void on_nav_recents(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    const char *curr = get_current_page();
    if (strcmp(curr, "shade") == 0)
        switch_to_page("home");
    else
        switch_to_page("shade");
}

static void on_toggle_drawer(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    const char *curr = get_current_page();
    if (strcmp(curr, "drawer") == 0)
        switch_to_page("home");
    else
        switch_to_page("drawer");
}

/* ------------------------------------------------------------------ */
/* Construtores de Componentes de UI                                  */
/* ------------------------------------------------------------------ */

static GtkWidget *create_app_tile(const char *icon_glyph, const char *name,
                                  const char *desc, const char *accent_class,
                                  GCallback cb, gpointer user_data)
{
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "app-tile");
    gtk_widget_add_css_class(btn, accent_class);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);

    GtkWidget *lbl_icon = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_icon),
        g_markup_printf_escaped("<span font='38'>%s</span>", icon_glyph));

    GtkWidget *lbl_name = gtk_label_new(name);
    gtk_widget_add_css_class(lbl_name, "app-tile-name");
    gtk_label_set_ellipsize(GTK_LABEL(lbl_name), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(lbl_name, 140, -1);

    GtkWidget *lbl_desc = gtk_label_new(desc);
    gtk_widget_add_css_class(lbl_desc, "app-tile-desc");
    gtk_label_set_ellipsize(GTK_LABEL(lbl_desc), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(lbl_desc, 140, -1);

    gtk_box_append(GTK_BOX(box), lbl_icon);
    gtk_box_append(GTK_BOX(box), lbl_name);
    gtk_box_append(GTK_BOX(box), lbl_desc);
    gtk_button_set_child(GTK_BUTTON(btn), box);

    if (cb)
        g_signal_connect(btn, "clicked", cb, user_data);

    return btn;
}

static GtkWidget *create_dock_icon(const char *icon_glyph, const char *tooltip,
                                  GCallback cb, gpointer user_data, gboolean is_center)
{
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, is_center ? "dock-btn-center" : "dock-btn");
    gtk_widget_set_tooltip_text(btn, tooltip);

    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl),
        g_markup_printf_escaped("<span font='%d'>%s</span>", is_center ? 32 : 28, icon_glyph));
    gtk_button_set_child(GTK_BUTTON(btn), lbl);

    if (cb)
        g_signal_connect(btn, "clicked", cb, user_data);

    return btn;
}

static GtkWidget *create_shade_tile(const char *icon, const char *title, GtkWidget **out_sub, GCallback cb)
{
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "shade-tile");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);

    GtkWidget *l_icon = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(l_icon), g_markup_printf_escaped("<span font='28'>%s</span>", icon));
    GtkWidget *l_title = gtk_label_new(title);
    gtk_widget_add_css_class(l_title, "shade-tile-title");

    GtkWidget *l_sub = gtk_label_new("…");
    gtk_widget_add_css_class(l_sub, "shade-tile-sub");
    if (out_sub) *out_sub = l_sub;

    gtk_box_append(GTK_BOX(box), l_icon);
    gtk_box_append(GTK_BOX(box), l_title);
    gtk_box_append(GTK_BOX(box), l_sub);
    gtk_button_set_child(GTK_BUTTON(btn), box);

    if (cb) g_signal_connect(btn, "clicked", cb, NULL);
    return btn;
}

static GtkWidget *create_slider_box(const char *icon, const char *label, double initial_val, GCallback cb)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box, "slider-card");

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *l_icon = gtk_label_new(icon);
    GtkWidget *l_txt = gtk_label_new(label);
    gtk_widget_add_css_class(l_txt, "slider-label");
    gtk_box_append(GTK_BOX(row), l_icon);
    gtk_box_append(GTK_BOX(row), l_txt);
    gtk_box_append(GTK_BOX(box), row);

    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(scale), initial_val);
    gtk_widget_set_size_request(scale, -1, 48);
    g_signal_connect(scale, "value-changed", cb, NULL);
    gtk_box_append(GTK_BOX(box), scale);

    return box;
}

/* ------------------------------------------------------------------ */
/* Inicialização da Tela Principal (Activator)                        */
/* ------------------------------------------------------------------ */

static void activate(GtkApplication *application, gpointer user_data G_GNUC_UNUSED)
{
    /* Descoberta de aplicativos .desktop (Fase 2) — antes de montar a UI */
    load_applications();

    GtkWidget *win = gtk_application_window_new(application);
    app.window = win;
    gtk_window_set_title(GTK_WINDOW(win), "Android Launcher");
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), 1080, 1920);

    /* Container Raiz */
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root, "root-canvas");
    gtk_window_set_child(GTK_WINDOW(win), root);

    /* ============================================================== */
    /* 1. Barra de Status Superior Fixa (Android Top Bar)             */
    /* ============================================================== */
    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(status_bar, "status-bar");
    gtk_widget_set_size_request(status_bar, -1, 56);

    app.lbl_status_time = gtk_label_new("12:00");
    gtk_widget_add_css_class(app.lbl_status_time, "status-time");
    gtk_box_append(GTK_BOX(status_bar), app.lbl_status_time);

    GtkWidget *status_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(status_spacer, TRUE);
    gtk_box_append(GTK_BOX(status_bar), status_spacer);

    app.lbl_status_wifi = gtk_label_new("📶 --");
    gtk_widget_add_css_class(app.lbl_status_wifi, "status-item");
    gtk_box_append(GTK_BOX(status_bar), app.lbl_status_wifi);

    app.lbl_status_bt = gtk_label_new("ᛒ --");
    gtk_widget_add_css_class(app.lbl_status_bt, "status-item");
    gtk_box_append(GTK_BOX(status_bar), app.lbl_status_bt);

    app.lbl_status_vol = gtk_label_new("🔊 --");
    gtk_widget_add_css_class(app.lbl_status_vol, "status-item");
    gtk_box_append(GTK_BOX(status_bar), app.lbl_status_vol);

    app.lbl_status_temp = gtk_label_new("🌡️ --");
    gtk_widget_add_css_class(app.lbl_status_temp, "status-item");
    gtk_box_append(GTK_BOX(status_bar), app.lbl_status_temp);

    /* Toque na barra de status abre o Quick Settings Shade */
    GtkWidget *status_btn = gtk_button_new();
    gtk_widget_add_css_class(status_btn, "status-btn-overlay");
    gtk_button_set_child(GTK_BUTTON(status_btn), status_bar);
    g_signal_connect_swapped(status_btn, "clicked", G_CALLBACK(switch_to_page), (gpointer)"shade");
    gtk_box_append(GTK_BOX(root), status_btn);

    /* ============================================================== */
    /* 2. Stack Principal de Páginas (GtkStack)                       */
    /* ============================================================== */
    GtkWidget *stack = gtk_stack_new();
    app.stack = stack;
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_UP_DOWN);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 250);
    gtk_box_append(GTK_BOX(root), stack);

    /* -------------------------------------------------------------- */
    /* PÁGINA A: "home" (Tela Inicial Android)                        */
    /* -------------------------------------------------------------- */
    GtkWidget *page_home = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(page_home, 32);
    gtk_widget_set_margin_end(page_home, 32);
    gtk_widget_set_margin_top(page_home, 24);
    gtk_widget_set_margin_bottom(page_home, 16);

    /* Widget At-a-Glance */
    GtkWidget *clock_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(clock_card, "at-a-glance-card");
    gtk_widget_set_halign(clock_card, GTK_ALIGN_CENTER);

    app.lbl_widget_time = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(clock_card), app.lbl_widget_time);

    app.lbl_widget_date = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(clock_card), app.lbl_widget_date);

    app.lbl_widget_chip = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(app.lbl_widget_chip),
        "<span font='13' weight='500' color='#9AA0A6'>⚡ Moto G5 Plus • Snapdragon 625 • Adreno 506</span>");
    gtk_widget_add_css_class(app.lbl_widget_chip, "chip-badge");
    gtk_widget_set_halign(app.lbl_widget_chip, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(app.lbl_widget_chip, 8);
    gtk_box_append(GTK_BOX(clock_card), app.lbl_widget_chip);
    gtk_box_append(GTK_BOX(page_home), clock_card);

    /* Barra de Pesquisa Pill estilo Google Pixel → abre a Gaveta com teclado */
    GtkWidget *search_pill = gtk_button_new();
    gtk_widget_add_css_class(search_pill, "search-pill");
    GtkWidget *pill_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(pill_box, 18);
    gtk_widget_set_margin_end(pill_box, 18);
    GtkWidget *pill_icon = gtk_label_new("🔍");
    GtkWidget *pill_txt = gtk_label_new("Buscar aplicativos...");
    gtk_widget_add_css_class(pill_txt, "search-pill-txt");
    gtk_widget_set_hexpand(pill_txt, TRUE);
    gtk_widget_set_halign(pill_txt, GTK_ALIGN_START);
    GtkWidget *pill_tune = gtk_label_new("⚡");
    gtk_box_append(GTK_BOX(pill_box), pill_icon);
    gtk_box_append(GTK_BOX(pill_box), pill_txt);
    gtk_box_append(GTK_BOX(pill_box), pill_tune);
    gtk_button_set_child(GTK_BUTTON(search_pill), pill_box);
    g_signal_connect(search_pill, "clicked", G_CALLBACK(on_open_drawer_search), NULL);
    gtk_box_append(GTK_BOX(page_home), search_pill);

    /* Grade 2x2 de Cards Principais */
    GtkWidget *home_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(home_grid), 20);
    gtk_grid_set_column_spacing(GTK_GRID(home_grid), 20);
    gtk_grid_set_row_homogeneous(GTK_GRID(home_grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(home_grid), TRUE);
    gtk_widget_set_vexpand(home_grid, TRUE);
    gtk_box_append(GTK_BOX(page_home), home_grid);

    GtkWidget *t_mus = create_app_tile("🎵", "Música", "Player MP3 Hi-Fi", "tile-music",
                                       G_CALLBACK(on_open_music), NULL);
    gtk_grid_attach(GTK_GRID(home_grid), t_mus, 0, 0, 1, 1);

    GtkWidget *t_set = create_app_tile("⚙️", "Configurações", "Wi-Fi, Som & Tela", "tile-settings",
                                       G_CALLBACK(switch_to_page), (gpointer)"shade");
    gtk_grid_attach(GTK_GRID(home_grid), t_set, 1, 0, 1, 1);

    GtkWidget *t_trm = create_app_tile("💻", "Terminal", "Shell Bash ARM64", "tile-terminal",
                                       G_CALLBACK(on_open_terminal), NULL);
    gtk_grid_attach(GTK_GRID(home_grid), t_trm, 0, 1, 1, 1);

    GtkWidget *t_sto = create_app_tile("📁", "Armazenamento", "eMMC 25GB & SD", "tile-storage",
                                       G_CALLBACK(show_storage_dialog), win);
    gtk_grid_attach(GTK_GRID(home_grid), t_sto, 1, 1, 1, 1);

    /* Dock Inferior de Favoritos */
    GtkWidget *dock_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(dock_box, GTK_ALIGN_CENTER);

    GtkWidget *dock = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(dock, "app-dock");

    GtkWidget *d1 = create_dock_icon("💻", "Terminal", G_CALLBACK(on_open_terminal), NULL, FALSE);
    GtkWidget *d2 = create_dock_icon("🎵", "Música", G_CALLBACK(on_open_music), NULL, FALSE);
    GtkWidget *d_center = create_dock_icon("⊞", "Gaveta de Apps", G_CALLBACK(on_toggle_drawer), NULL, TRUE);
    GtkWidget *d3 = create_dock_icon("📁", "Arquivos", G_CALLBACK(show_storage_dialog), win, FALSE);
    GtkWidget *d4 = create_dock_icon("⚙️", "Configurações", G_CALLBACK(switch_to_page), (gpointer)"shade", FALSE);

    gtk_box_append(GTK_BOX(dock), d1);
    gtk_box_append(GTK_BOX(dock), d2);
    gtk_box_append(GTK_BOX(dock), d_center);
    gtk_box_append(GTK_BOX(dock), d3);
    gtk_box_append(GTK_BOX(dock), d4);
    gtk_box_append(GTK_BOX(dock_box), dock);
    gtk_box_append(GTK_BOX(page_home), dock_box);

    gtk_stack_add_named(GTK_STACK(stack), page_home, "home");

    /* -------------------------------------------------------------- */
    /* PÁGINA B: "drawer" (Gaveta Dinâmica de Todos os Aplicativos)   */
    /* -------------------------------------------------------------- */
    GtkWidget *page_drawer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(page_drawer, 24);
    gtk_widget_set_margin_end(page_drawer, 24);
    gtk_widget_set_margin_top(page_drawer, 20);
    gtk_widget_set_margin_bottom(page_drawer, 0);

    GtkWidget *drawer_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *lbl_dr_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_dr_title),
        "<span font='24' weight='bold'>⊞ Todos os Aplicativos</span>");
    gtk_widget_set_hexpand(lbl_dr_title, TRUE);
    gtk_widget_set_halign(lbl_dr_title, GTK_ALIGN_START);

    app.lbl_dr_count = gtk_label_new(NULL);
    gtk_widget_set_valign(app.lbl_dr_count, GTK_ALIGN_CENTER);

    GtkWidget *btn_osk_toggle = gtk_button_new_with_label("⌨");
    gtk_widget_add_css_class(btn_osk_toggle, "btn-circle-close");
    g_signal_connect(btn_osk_toggle, "clicked", G_CALLBACK(on_osk_toggle_button), NULL);

    GtkWidget *btn_close_drawer = gtk_button_new_with_label("✕");
    gtk_widget_add_css_class(btn_close_drawer, "btn-circle-close");
    g_signal_connect_swapped(btn_close_drawer, "clicked", G_CALLBACK(switch_to_page), (gpointer)"home");

    gtk_box_append(GTK_BOX(drawer_header), lbl_dr_title);
    gtk_box_append(GTK_BOX(drawer_header), app.lbl_dr_count);
    gtk_box_append(GTK_BOX(drawer_header), btn_osk_toggle);
    gtk_box_append(GTK_BOX(drawer_header), btn_close_drawer);
    gtk_box_append(GTK_BOX(page_drawer), drawer_header);

    /* Campo de Busca em tempo real */
    app.dr_search = gtk_search_entry_new();
    gtk_widget_add_css_class(app.dr_search, "search-entry");
    gtk_widget_set_size_request(app.dr_search, -1, 60);
    g_signal_connect(app.dr_search, "search-changed", G_CALLBACK(on_search_changed), NULL);
    g_signal_connect(app.dr_search, "activate", G_CALLBACK(on_search_activate), NULL);
    gtk_box_append(GTK_BOX(page_drawer), app.dr_search);

    /* Placeholder + foco no GtkText interno → sobe o teclado virtual */
    GtkWidget *search_text = gtk_widget_get_first_child(app.dr_search);
    if (GTK_IS_TEXT(search_text)) {
        gtk_text_set_placeholder_text(GTK_TEXT(search_text), "Buscar aplicativos…");
        g_signal_connect(search_text, "notify::has-focus", G_CALLBACK(on_entry_focus_changed), NULL);
    }

    /* Estado vazio da busca */
    app.dr_empty = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(app.dr_empty),
        "<span font='20' color='#9AA0A6'>🔍 Nenhum aplicativo encontrado\n"
        "<span font='15'>Tente outro termo de busca</span></span>");
    gtk_label_set_justify(GTK_LABEL(app.dr_empty), GTK_JUSTIFY_CENTER);
    gtk_widget_set_margin_top(app.dr_empty, 60);
    gtk_widget_set_visible(app.dr_empty, FALSE);
    gtk_box_append(GTK_BOX(page_drawer), app.dr_empty);

    /* Grade dinâmica rolável (3 colunas, ordenada A→Z) */
    app.dr_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(app.dr_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app.dr_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_append(GTK_BOX(page_drawer), app.dr_scroll);

    /* Teclado Virtual Touch (Fase 2, Opção B do plano) */
    gtk_box_append(GTK_BOX(page_drawer), build_osk());

    gtk_stack_add_named(GTK_STACK(stack), page_drawer, "drawer");

    rebuild_drawer_grid();

    /* -------------------------------------------------------------- */
    /* PÁGINA C: "shade" (Quick Settings Shade Estilo Android)        */
    /* -------------------------------------------------------------- */
    GtkWidget *page_shade = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page_shade, 28);
    gtk_widget_set_margin_end(page_shade, 28);
    gtk_widget_set_margin_top(page_shade, 20);
    gtk_widget_set_margin_bottom(page_shade, 16);

    GtkWidget *shade_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *lbl_sh_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_sh_title),
        "<span font='24' weight='bold'>⚡ Configurações Rápidas</span>");
    gtk_widget_set_hexpand(lbl_sh_title, TRUE);
    gtk_widget_set_halign(lbl_sh_title, GTK_ALIGN_START);

    GtkWidget *btn_close_shade = gtk_button_new_with_label("✕");
    gtk_widget_add_css_class(btn_close_shade, "btn-circle-close");
    g_signal_connect_swapped(btn_close_shade, "clicked", G_CALLBACK(switch_to_page), (gpointer)"home");

    gtk_box_append(GTK_BOX(shade_header), lbl_sh_title);
    gtk_box_append(GTK_BOX(shade_header), btn_close_shade);
    gtk_box_append(GTK_BOX(page_shade), shade_header);

    /* Grade 2x2 de Tiles Grandes */
    GtkWidget *shade_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(shade_grid), 16);
    gtk_grid_set_column_spacing(GTK_GRID(shade_grid), 16);
    gtk_grid_set_row_homogeneous(GTK_GRID(shade_grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(shade_grid), TRUE);

    app.btn_shade_wifi = create_shade_tile("📶", "Wi-Fi", &app.lbl_shade_wifi_sub, G_CALLBACK(on_toggle_wifi));
    app.btn_shade_bt   = create_shade_tile("ᛒ", "Bluetooth", &app.lbl_shade_bt_sub, G_CALLBACK(on_toggle_bt));
    app.btn_shade_air  = create_shade_tile("✈", "Modo Avião", NULL, G_CALLBACK(on_toggle_airplane));
    app.btn_shade_sleep = create_shade_tile("🌙", "Apagar Tela", NULL, G_CALLBACK(enter_sleep_lock));

    gtk_grid_attach(GTK_GRID(shade_grid), app.btn_shade_wifi, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(shade_grid), app.btn_shade_bt,   1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(shade_grid), app.btn_shade_air,  0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(shade_grid), app.btn_shade_sleep, 1, 1, 1, 1);
    gtk_box_append(GTK_BOX(page_shade), shade_grid);

    /* Sliders de Brilho e Volume */
    GtkWidget *sb_bl = create_slider_box("☀", "Brilho da Tela", backlight_get(), G_CALLBACK(on_shade_bl_changed));
    app.scale_shade_bl = gtk_widget_get_last_child(sb_bl);
    gtk_box_append(GTK_BOX(page_shade), sb_bl);

    GtkWidget *sb_vol = create_slider_box("🔊", "Volume do Alto-falante", volume_get(), G_CALLBACK(on_shade_vol_changed));
    app.scale_shade_vol = gtk_widget_get_last_child(sb_vol);
    gtk_box_append(GTK_BOX(page_shade), sb_vol);

    /* Ações de Energia Rápidas */
    GtkWidget *shade_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_margin_top(shade_actions, 10);

    GtkWidget *btn_sh_reb = gtk_button_new_with_label("⟳ Reiniciar");
    gtk_widget_add_css_class(btn_sh_reb, "btn-power-reboot");
    gtk_widget_set_hexpand(btn_sh_reb, TRUE);
    gtk_widget_set_size_request(btn_sh_reb, -1, 56);
    g_signal_connect_swapped(btn_sh_reb, "clicked", G_CALLBACK(launch_cmd_async), "systemctl reboot");

    GtkWidget *btn_sh_off = gtk_button_new_with_label("⏻ Desligar");
    gtk_widget_add_css_class(btn_sh_off, "btn-power-off");
    gtk_widget_set_hexpand(btn_sh_off, TRUE);
    gtk_widget_set_size_request(btn_sh_off, -1, 56);
    g_signal_connect_swapped(btn_sh_off, "clicked", G_CALLBACK(launch_cmd_async), "systemctl poweroff");

    gtk_box_append(GTK_BOX(shade_actions), btn_sh_reb);
    gtk_box_append(GTK_BOX(shade_actions), btn_sh_off);
    gtk_box_append(GTK_BOX(page_shade), shade_actions);

    gtk_stack_add_named(GTK_STACK(stack), page_shade, "shade");

    /* -------------------------------------------------------------- */
    /* PÁGINA D: "lock" (Tela de Bloqueio & Ambient Display)          */
    /* -------------------------------------------------------------- */
    GtkWidget *page_lock = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_valign(page_lock, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(page_lock, GTK_ALIGN_CENTER);

    app.lbl_lock_time = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(page_lock), app.lbl_lock_time);

    app.lbl_lock_date = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(page_lock), app.lbl_lock_date);

    GtkWidget *lock_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(lock_card, "lock-card");
    gtk_widget_set_margin_top(lock_card, 40);

    GtkWidget *btn_unlock = gtk_button_new();
    gtk_widget_add_css_class(btn_unlock, "btn-unlock");
    gtk_widget_set_size_request(btn_unlock, 320, 80);

    GtkWidget *ubox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(ubox, GTK_ALIGN_CENTER);
    GtkWidget *u_icon = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(u_icon), "<span font='32'>🔓</span>");
    GtkWidget *u_txt = gtk_label_new("Toque para Desbloquear");
    gtk_widget_add_css_class(u_txt, "btn-unlock-txt");
    gtk_box_append(GTK_BOX(ubox), u_icon);
    gtk_box_append(GTK_BOX(ubox), u_txt);
    gtk_button_set_child(GTK_BUTTON(btn_unlock), ubox);
    g_signal_connect(btn_unlock, "clicked", G_CALLBACK(unlock_screen), NULL);

    gtk_box_append(GTK_BOX(lock_card), btn_unlock);
    gtk_box_append(GTK_BOX(page_lock), lock_card);

    gtk_stack_add_named(GTK_STACK(stack), page_lock, "lock");

    /* ============================================================== */
    /* 3. Barra de Navegação Android 3-Button (Rodapé Fixo)           */
    /* ============================================================== */
    GtkWidget *nav_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(nav_bar, "android-navbar");
    gtk_widget_set_size_request(nav_bar, -1, 56);

    GtkWidget *btn_back = gtk_button_new_with_label("◀");
    gtk_widget_add_css_class(btn_back, "navbar-btn");
    gtk_widget_set_hexpand(btn_back, TRUE);
    g_signal_connect(btn_back, "clicked", G_CALLBACK(on_nav_back), NULL);

    GtkWidget *btn_home = gtk_button_new_with_label("⏺");
    gtk_widget_add_css_class(btn_home, "navbar-btn");
    gtk_widget_set_hexpand(btn_home, TRUE);
    g_signal_connect(btn_home, "clicked", G_CALLBACK(on_nav_home), NULL);

    GtkWidget *btn_recents = gtk_button_new_with_label("⏹");
    gtk_widget_add_css_class(btn_recents, "navbar-btn");
    gtk_widget_set_hexpand(btn_recents, TRUE);
    g_signal_connect(btn_recents, "clicked", G_CALLBACK(on_nav_recents), NULL);

    gtk_box_append(GTK_BOX(nav_bar), btn_back);
    gtk_box_append(GTK_BOX(nav_bar), btn_home);
    gtk_box_append(GTK_BOX(nav_bar), btn_recents);
    gtk_box_append(GTK_BOX(root), nav_bar);

    /* ============================================================== */
    /* 4. CSS Estilizado Material You Dark Completo                   */
    /* ============================================================== */
    const char *css =
        /* Paleta AOSP Launcher3 dark (colors.xml do AOSP): superfícies #202124/#292A2D/#303030,
           Grey 800 #3C4043, texto #E3E3E3, acento #8AB4F8/#A8C7FA — sem cards coloridos. */
        "window { background-color: #202124; }\n"
        ".root-canvas { background-color: #202124; }\n"
        ".status-btn-overlay { background: transparent; border: none; padding: 0; margin: 0; }\n"
        ".status-bar {\n"
        "  background-color: rgba(32, 33, 36, 0.98);\n"
        "  padding: 8px 24px;\n"
        "  border-bottom: none;\n"
        "}\n"
        ".status-time { color: #E3E3E3; font-size: 17px; font-weight: 500; }\n"
        ".status-item { color: #E3E3E3; font-size: 14px; font-weight: 400; margin-left: 8px; }\n"
        "\n"
        ".at-a-glance-card { padding: 12px 24px; }\n"
        ".chip-badge {\n"
        "  background-color: #292A2D;\n"
        "  border: none;\n"
        "  border-radius: 20px;\n"
        "  padding: 6px 16px;\n"
        "}\n"
        "\n"
        ".search-pill {\n"
        "  background-color: #303030;\n"
        "  border: none;\n"
        "  border-radius: 24px;\n"
        "  min-height: 54px;\n"
        "  padding: 4px;\n"
        "}\n"
        ".search-pill:active {\n"
        "  background-color: #3C4043;\n"
        "}\n"
        ".search-pill-txt { color: #9AA0A6; font-size: 15px; }\n"
        "\n"
        "/* AOSP: ícones soltos na grade, SEM card colorido (rótulo branco embaixo) */\n"
        ".app-tile {\n"
        "  background-color: transparent;\n"
        "  border: none;\n"
        "  border-radius: 16px;\n"
        "  padding: 12px;\n"
        "  min-height: 110px;\n"
        "  transition: all 150ms ease-out;\n"
        "}\n"
        ".app-tile:active { background-color: #2E3134; }\n"
        ".tile-music { background-color: transparent; border: none; }\n"
        ".tile-settings { background-color: transparent; border: none; }\n"
        ".tile-terminal { background-color: transparent; border: none; }\n"
        ".tile-storage { background-color: transparent; border: none; }\n"
        ".tile-info { background-color: transparent; border: none; }\n"
        ".tile-power { background-color: transparent; border: none; }\n"
        "\n"
        ".app-tile-name { color: #FFFFFF; font-size: 15px; font-weight: 500; margin-top: 6px; }\n"
        ".app-tile-desc { color: #9AA0A6; font-size: 11px; font-weight: 400; }\n"
        "\n"
        "/* Hotseat AOSP: sem pílula, sem borda — só os ícones */\n"
        ".app-dock {\n"
        "  background-color: transparent;\n"
        "  border: none;\n"
        "  border-radius: 0;\n"
        "  padding: 10px 20px;\n"
        "}\n"
        ".dock-btn {\n"
        "  background-color: transparent;\n"
        "  border: none;\n"
        "  border-radius: 16px;\n"
        "  min-width: 60px;\n"
        "  min-height: 60px;\n"
        "  padding: 0;\n"
        "}\n"
        ".dock-btn:active { background-color: #2E3134; }\n"
        ".dock-btn-center {\n"
        "  background-color: transparent;\n"
        "  border: none;\n"
        "  border-radius: 16px;\n"
        "  min-width: 68px;\n"
        "  min-height: 68px;\n"
        "  padding: 0;\n"
        "}\n"
        ".dock-btn-center:active { background-color: #2E3134; }\n"
        "\n"
        "/* Quick Settings estilo Android 12+: tile escuro, ativo = acento AOSP */\n"
        ".shade-tile {\n"
        "  background-color: #292A2D;\n"
        "  border: none;\n"
        "  border-radius: 24px;\n"
        "  min-height: 110px;\n"
        "  padding: 12px;\n"
        "}\n"
        ".shade-tile.tile-active {\n"
        "  background-color: #8AB4F8;\n"
        "  border: none;\n"
        "}\n"
        ".shade-tile-title { color: #E3E3E3; font-size: 17px; font-weight: 500; }\n"
        ".shade-tile-sub { color: #9AA0A6; font-size: 13px; }\n"
        "\n"
        ".slider-card {\n"
        "  background-color: #292A2D;\n"
        "  border: none;\n"
        "  border-radius: 24px;\n"
        "  padding: 14px 18px;\n"
        "}\n"
        ".slider-label { color: #E3E3E3; font-size: 15px; font-weight: 500; }\n"
        "scale trough { background: #3C4043; border-radius: 6px; min-height: 12px; }\n"
        "scale highlight { background: #8AB4F8; border-radius: 6px; }\n"
        "scale slider { background: #FFFFFF; border-radius: 14px; min-width: 28px; min-height: 28px; }\n"
        "\n"
        ".btn-circle-close {\n"
        "  background-color: #3C4043;\n"
        "  color: #E3E3E3;\n"
        "  border-radius: 20px;\n"
        "  min-width: 40px;\n"
        "  min-height: 40px;\n"
        "  font-size: 18px;\n"
        "  border: none;\n"
        "}\n"
        "\n"
        ".btn-unlock {\n"
        "  background-color: #A8C7FA;\n"
        "  border: none;\n"
        "  border-radius: 36px;\n"
        "  color: #062E6F;\n"
        "}\n"
        ".btn-unlock-txt { font-size: 20px; font-weight: bold; color: #062E6F; }\n"
        "\n"
        "/* Nav bar AOSP: fundo escuro, sem borda, ícones neutros */\n"
        ".android-navbar {\n"
        "  background-color: #202124;\n"
        "  border-top: none;\n"
        "}\n"
        ".navbar-btn {\n"
        "  background: transparent;\n"
        "  border: none;\n"
        "  color: #E3E3E3;\n"
        "  font-size: 24px;\n"
        "}\n"
        ".navbar-btn:active { background-color: #3C4043; }\n"
        "\n"
        ".info-card {\n"
        "  background-color: #292A2D;\n"
        "  border: none;\n"
        "  border-radius: 16px;\n"
        "  padding: 18px;\n"
        "  color: #E3E3E3;\n"
        "  font-size: 15px;\n"
        "}\n"
        ".btn-action {\n"
        "  background-color: #3C4043;\n"
        "  color: #E3E3E3;\n"
        "  border-radius: 100px;\n"
        "  font-size: 16px;\n"
        "  font-weight: bold;\n"
        "  border: none;\n"
        "}\n"
        ".btn-power-reboot {\n"
        "  background-color: #8AB4F8;\n"
        "  color: #062E6F;\n"
        "  border-radius: 100px;\n"
        "  font-size: 17px;\n"
        "  font-weight: bold;\n"
        "  border: none;\n"
        "}\n"
        "/* Botão destrutivo no container de erro do Material dark */\n"
        ".btn-power-off {\n"
        "  background-color: #F2B8B5;\n"
        "  color: #601410;\n"
        "  border-radius: 100px;\n"
        "  font-size: 17px;\n"
        "  font-weight: bold;\n"
        "  border: none;\n"
        "}\n"
        "\n"
        "/* ---------- Fase 2: Gaveta dinâmica, busca e teclado (AOSP dark) ---------- */\n"
        ".search-entry {\n"
        "  background-color: #303030;\n"
        "  border: none;\n"
        "  border-radius: 24px;\n"
        "  min-height: 60px;\n"
        "  padding: 0 20px;\n"
        "  color: #E3E3E3;\n"
        "  font-size: 17px;\n"
        "  caret-color: #8AB4F8;\n"
        "}\n"
        ".search-entry:focus {\n"
        "  background-color: #3C4043;\n"
        "  border: none;\n"
        "}\n"
        ".search-entry text { color: #E3E3E3; }\n"
        ".search-entry text > placeholder { color: #9AA0A6; font-size: 16px; }\n"
        ".search-entry image { color: #9AA0A6; }\n"
        "\n"
        ".drawer-tile { min-height: 128px; }\n"
        "\n"
        "/* Teclado Gboard: fundo dark, teclas neutras, funcionais em azul */\n"
        ".osk {\n"
        "  background-color: #202124;\n"
        "  border-top: none;\n"
        "  border-radius: 24px 24px 0 0;\n"
        "}\n"
        ".osk-key {\n"
        "  background-color: #4A4C51;\n"
        "  border: none;\n"
        "  border-radius: 10px;\n"
        "  color: #E3E3E3;\n"
        "  min-height: 68px;\n"
        "  min-width: 68px;\n"
        "  padding: 0 6px;\n"
        "  transition: all 100ms ease-out;\n"
        "}\n"
        ".osk-key:active { background-color: #5F6368; }\n"
        ".osk-key-lbl { color: #E3E3E3; font-size: 21px; font-weight: 500; }\n"
        ".osk-key-wide {\n"
        "  min-width: 104px;\n"
        "  background-color: #333537;\n"
        "}\n"
        ".osk-shift-on { background-color: #8AB4F8; }\n"
        ".osk-key-accent { background-color: #1A73E8; }\n"
        ".osk-key-accent:active { background-color: #1B66C9; }\n"
        ".osk-key-space { min-width: 120px; background-color: #333537; }\n";

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    /* Disparo inicial dos dados e timers */
    switch_to_page("home");
    update_clock();
    update_telemetry();
    g_timeout_add_seconds(1, on_second_timer, NULL);
    g_timeout_add_seconds(3, on_telemetry_timer, NULL);

    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv)
{
    backlight_init();
    GtkApplication *gtk_app = gtk_application_new(
        "br.sanders.androidlauncher", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}
