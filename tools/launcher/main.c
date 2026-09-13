/*
 * main.c — Orquestrador do Android Mobile Shell & Launcher GTK4
 * Arquitetura Modular Multi-Agentes
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

#include "app_drawer.h"
#include "window_manager.h"

/* ------------------------------------------------------------------ */
/* Utilitários de execução e telemetria                              */
/* ------------------------------------------------------------------ */

static void launch_tracked_cmd(const char *cmd, const char *name, const char *icon)
{
    GError *err = NULL;
    char *argv_cmd[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    GPid pid = 0;
    if (g_spawn_async(NULL, argv_cmd, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                      NULL, NULL, &pid, &err)) {
        wm_register_launched_app(pid, name, icon, cmd);
    } else {
        g_warning("Falha ao executar '%s': %s", cmd, err ? err->message : "erro desconhecido");
        if (err) g_error_free(err);
    }
}

static void on_drawer_app_launch(const char *cmd)
{
    launch_tracked_cmd(cmd, "Aplicativo", "📱");
}

static char *run_get_first_line(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    static char buf[256];
    buf[0] = '\0';
    if (fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\r\n")] = '\0';
    }
    pclose(fp);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Backlight & Volume                                                 */
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
        if (fscanf(f, "%d", &v) == 1) bl_max = v;
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

static double volume_get(void)
{
    char *res = run_get_first_line("amixer -c 0 sget 'RX1 Digital' 2>/dev/null | grep -oE '\\[[0-9]+%\\]' | head -n1 | tr -d '[]%'");
    if (res && res[0]) return atoi(res) / 100.0;
    return 0.75;
}

static void volume_set(double frac)
{
    int pct = (int)(frac * 100.0 + 0.5);
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "amixer -c 0 sset 'RX1 Digital' %d%% >/dev/null 2>&1; "
             "amixer -c 0 sset 'RX2 Digital' %d%% >/dev/null 2>&1", pct, pct);
    GError *err = NULL;
    char *argv_cmd[] = { "/bin/sh", "-c", cmd, NULL };
    g_spawn_async(NULL, argv_cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) g_error_free(err);
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
    if (res && res[0]) return res;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Estado da Aplicação                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    GtkWidget *window;
    GtkWidget *stack;
    
    GtkWidget *lbl_status_time;
    GtkWidget *lbl_status_wifi;
    GtkWidget *lbl_status_bt;
    GtkWidget *lbl_status_vol;
    GtkWidget *lbl_status_temp;

    GtkWidget *lbl_widget_time;
    GtkWidget *lbl_widget_date;
    GtkWidget *lbl_widget_chip;

    GtkWidget *btn_shade_wifi;
    GtkWidget *lbl_shade_wifi_sub;
    GtkWidget *btn_shade_bt;
    GtkWidget *lbl_shade_bt_sub;
    GtkWidget *btn_shade_air;
    GtkWidget *scale_shade_bl;
    GtkWidget *scale_shade_vol;

    GtkWidget *lbl_lock_time;
    GtkWidget *lbl_lock_date;

    gboolean updating_ui;
} MainApp;

static MainApp app;

static void switch_to_page(const char *name)
{
    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), name);
}

static const char *get_current_page(void)
{
    return gtk_stack_get_visible_child_name(GTK_STACK(app.stack));
}

static void on_return_home_cb(void)
{
    switch_to_page("home");
}

/* ------------------------------------------------------------------ */
/* Atualização de Relógio e Telemetria                                */
/* ------------------------------------------------------------------ */

static void update_clock(void)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M", t);
    gtk_label_set_text(GTK_LABEL(app.lbl_status_time), time_str);

    char big_time[128];
    snprintf(big_time, sizeof(big_time), "<span font='84' weight='bold' letter_spacing='-1000'>%s</span>", time_str);
    gtk_label_set_markup(GTK_LABEL(app.lbl_widget_time), big_time);

    char lock_time[128];
    snprintf(lock_time, sizeof(lock_time), "<span font='96' weight='bold' letter_spacing='-1500'>%s</span>", time_str);
    gtk_label_set_markup(GTK_LABEL(app.lbl_lock_time), lock_time);

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
    GtkStyleContext *ctx = gtk_widget_get_style_context(btn);
    if (active)
        gtk_style_context_add_class(ctx, "tile-active");
    else
        gtk_style_context_remove_class(ctx, "tile-active");
}

static void update_telemetry(void)
{
    app.updating_ui = TRUE;

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

    int bt = rfkill_state("hci0");
    gtk_label_set_text(GTK_LABEL(app.lbl_status_bt), (bt == 1) ? "ᛒ On" : "ᛒ Off");
    gtk_label_set_text(GTK_LABEL(app.lbl_shade_bt_sub), (bt == 1) ? "Ativo" : "Desligado");
    set_tile_active_class(app.btn_shade_bt, bt == 1);
    set_tile_active_class(app.btn_shade_air, wf == 0 && bt == 0);

    double vol = volume_get();
    char vstr[32];
    snprintf(vstr, sizeof(vstr), "🔊 %d%%", (int)(vol * 100 + 0.5));
    gtk_label_set_text(GTK_LABEL(app.lbl_status_vol), vstr);
    if (GTK_IS_RANGE(app.scale_shade_vol))
        gtk_range_set_value(GTK_RANGE(app.scale_shade_vol), vol);

    double bl = backlight_get();
    if (GTK_IS_RANGE(app.scale_shade_bl))
        gtk_range_set_value(GTK_RANGE(app.scale_shade_bl), bl);

    int temp = get_thermal_temp();
    if (temp > 0) {
        char tstr[32];
        snprintf(tstr, sizeof(tstr), "🌡️ %d°C", temp);
        gtk_label_set_text(GTK_LABEL(app.lbl_status_temp), tstr);
    }

    app.updating_ui = FALSE;
}

static gboolean on_second_timer(gpointer d G_GNUC_UNUSED) { update_clock(); return G_SOURCE_CONTINUE; }
static gboolean on_telemetry_timer(gpointer d G_GNUC_UNUSED) { update_telemetry(); return G_SOURCE_CONTINUE; }

static void on_toggle_wifi(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    rfkill_set("phy0", rfkill_state("phy0") != 1);
    update_telemetry();
}

static void on_toggle_bt(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    rfkill_set("hci0", rfkill_state("hci0") != 1);
    update_telemetry();
}

static void on_toggle_airplane(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    int wf = rfkill_state("phy0"), bt = rfkill_state("hci0");
    int to_air = !(wf == 0 && bt == 0);
    rfkill_set("phy0", !to_air);
    rfkill_set("hci0", !to_air);
    update_telemetry();
}

static void on_shade_bl_changed(GtkRange *r, gpointer d G_GNUC_UNUSED)
{
    if (!app.updating_ui) backlight_set(gtk_range_get_value(r));
}

static void on_shade_vol_changed(GtkRange *r, gpointer d G_GNUC_UNUSED)
{
    if (!app.updating_ui) volume_set(gtk_range_get_value(r));
}

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
/* Ações dos Botões da Barra de Navegação Android (3-Button Nav)      */
/* ------------------------------------------------------------------ */

static void on_nav_back(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    const char *curr = get_current_page();
    if (strcmp(curr, "home") != 0) {
        switch_to_page("home");
    } else {
        wm_action_back(GTK_WINDOW(app.window));
    }
}

static void on_nav_home(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    switch_to_page("home");
    wm_action_home(GTK_WINDOW(app.window));
}

static void on_nav_recents(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    const char *curr = get_current_page();
    if (strcmp(curr, "recents") == 0)
        switch_to_page("home");
    else
        switch_to_page("recents");
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
/* Launchers Rápidos                                                  */
/* ------------------------------------------------------------------ */

static void on_launch_music(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    launch_tracked_cmd("/root/mp3_player/player_gtk4 &", "Música Hi-Fi", "🎵");
}

static void on_launch_terminal(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    launch_tracked_cmd("weston-terminal &", "Terminal Bash", "💻");
}

/* Ações de energia (assinatura correta p/ signal "clicked": evita crash de args trocados) */
static void on_power_reboot(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    launch_tracked_cmd("systemctl reboot", "Reiniciar", "⟳");
}

static void on_power_off(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    launch_tracked_cmd("systemctl poweroff", "Desligar", "⏻");
}

/* Alterna o teclado virtual da gaveta (widget retornado por keyboard_create_popup) */
static void on_osk_toggle(GtkWidget *w G_GNUC_UNUSED, gpointer d)
{
    gtk_revealer_set_reveal_child(GTK_REVEALER(d),
                                  !gtk_revealer_get_reveal_child(GTK_REVEALER(d)));
}

static GtkWidget *create_app_tile(const char *icon, const char *name, const char *desc,
                                  const char *cls, GCallback cb, gpointer ud)
{
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "app-tile");
    gtk_widget_add_css_class(btn, cls);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);

    GtkWidget *lbl_i = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_i), g_markup_printf_escaped("<span font='38'>%s</span>", icon));
    GtkWidget *lbl_n = gtk_label_new(name);
    gtk_widget_add_css_class(lbl_n, "app-tile-name");
    GtkWidget *lbl_d = gtk_label_new(desc);
    gtk_widget_add_css_class(lbl_d, "app-tile-desc");

    gtk_box_append(GTK_BOX(box), lbl_i);
    gtk_box_append(GTK_BOX(box), lbl_n);
    gtk_box_append(GTK_BOX(box), lbl_d);
    gtk_button_set_child(GTK_BUTTON(btn), box);

    if (cb) g_signal_connect(btn, "clicked", cb, ud);
    return btn;
}

static GtkWidget *create_dock_btn(const char *icon, const char *tip, GCallback cb,
                                 gpointer ud, gboolean is_center)
{
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, is_center ? "dock-btn-center" : "dock-btn");
    gtk_widget_set_tooltip_text(btn, tip);

    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl),
        g_markup_printf_escaped("<span font='%d'>%s</span>", is_center ? 32 : 28, icon));
    gtk_button_set_child(GTK_BUTTON(btn), lbl);

    if (cb) g_signal_connect(btn, "clicked", cb, ud);
    return btn;
}

static GtkWidget *create_shade_btn(const char *icon, const char *title, GtkWidget **out_sub, GCallback cb)
{
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "shade-tile");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);

    GtkWidget *l_i = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(l_i), g_markup_printf_escaped("<span font='28'>%s</span>", icon));
    GtkWidget *l_t = gtk_label_new(title);
    gtk_widget_add_css_class(l_t, "shade-tile-title");
    GtkWidget *l_s = gtk_label_new("…");
    gtk_widget_add_css_class(l_s, "shade-tile-sub");
    if (out_sub) *out_sub = l_s;

    gtk_box_append(GTK_BOX(box), l_i);
    gtk_box_append(GTK_BOX(box), l_t);
    gtk_box_append(GTK_BOX(box), l_s);
    gtk_button_set_child(GTK_BUTTON(btn), box);
    if (cb) g_signal_connect(btn, "clicked", cb, NULL);
    return btn;
}

static GtkWidget *create_slider_row(const char *icon, const char *label, double val, GCallback cb)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box, "slider-card");

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *l_i = gtk_label_new(icon);
    GtkWidget *l_l = gtk_label_new(label);
    gtk_widget_add_css_class(l_l, "slider-label");
    gtk_box_append(GTK_BOX(row), l_i);
    gtk_box_append(GTK_BOX(row), l_l);
    gtk_box_append(GTK_BOX(box), row);

    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(scale), val);
    gtk_widget_set_size_request(scale, -1, 48);
    g_signal_connect(scale, "value-changed", cb, NULL);
    gtk_box_append(GTK_BOX(box), scale);
    return box;
}

/* ------------------------------------------------------------------ */
/* Inicialização da Interface Principal                               */
/* ------------------------------------------------------------------ */

static void activate(GtkApplication *application, gpointer ud G_GNUC_UNUSED)
{
    GtkWidget *win = gtk_application_window_new(application);
    app.window = win;
    gtk_window_set_title(GTK_WINDOW(win), "Android Launcher");
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), 1080, 1920);

    wm_init();

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root, "root-canvas");
    gtk_window_set_child(GTK_WINDOW(win), root);

    /* 1. Status Bar Superior */
    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(status_bar, "status-bar");
    gtk_widget_set_size_request(status_bar, -1, 56);

    app.lbl_status_time = gtk_label_new("12:00");
    gtk_widget_add_css_class(app.lbl_status_time, "status-time");
    gtk_box_append(GTK_BOX(status_bar), app.lbl_status_time);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(status_bar), spacer);

    app.lbl_status_wifi = gtk_label_new("📶 --");
    gtk_widget_add_css_class(app.lbl_status_wifi, "status-item");
    app.lbl_status_bt   = gtk_label_new("ᛒ --");
    gtk_widget_add_css_class(app.lbl_status_bt, "status-item");
    app.lbl_status_vol  = gtk_label_new("🔊 --");
    gtk_widget_add_css_class(app.lbl_status_vol, "status-item");
    app.lbl_status_temp = gtk_label_new("🌡️ --");
    gtk_widget_add_css_class(app.lbl_status_temp, "status-item");

    gtk_box_append(GTK_BOX(status_bar), app.lbl_status_wifi);
    gtk_box_append(GTK_BOX(status_bar), app.lbl_status_bt);
    gtk_box_append(GTK_BOX(status_bar), app.lbl_status_vol);
    gtk_box_append(GTK_BOX(status_bar), app.lbl_status_temp);

    GtkWidget *status_btn = gtk_button_new();
    gtk_widget_add_css_class(status_btn, "status-btn-overlay");
    gtk_button_set_child(GTK_BUTTON(status_btn), status_bar);
    g_signal_connect_swapped(status_btn, "clicked", G_CALLBACK(switch_to_page), (gpointer)"shade");
    gtk_box_append(GTK_BOX(root), status_btn);

    /* 2. GtkStack */
    GtkWidget *stack = gtk_stack_new();
    app.stack = stack;
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_UP_DOWN);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 250);
    gtk_box_append(GTK_BOX(root), stack);

    /* PÁGINA: "home" */
    GtkWidget *page_home = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(page_home, 32);
    gtk_widget_set_margin_end(page_home, 32);
    gtk_widget_set_margin_top(page_home, 24);
    gtk_widget_set_margin_bottom(page_home, 16);

    GtkWidget *clock_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(clock_card, "at-a-glance-card");
    gtk_widget_set_halign(clock_card, GTK_ALIGN_CENTER);

    app.lbl_widget_time = gtk_label_new(NULL);
    app.lbl_widget_date = gtk_label_new(NULL);
    app.lbl_widget_chip = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(app.lbl_widget_chip),
        "<span font='13' weight='bold' color='#64b5f6'>⚡ Moto G5 Plus • Snapdragon 625 • Adreno 506</span>");
    gtk_widget_add_css_class(app.lbl_widget_chip, "chip-badge");
    gtk_widget_set_halign(app.lbl_widget_chip, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(app.lbl_widget_chip, 8);

    gtk_box_append(GTK_BOX(clock_card), app.lbl_widget_time);
    gtk_box_append(GTK_BOX(clock_card), app.lbl_widget_date);
    gtk_box_append(GTK_BOX(clock_card), app.lbl_widget_chip);
    gtk_box_append(GTK_BOX(page_home), clock_card);

    GtkWidget *search_pill = gtk_button_new();
    gtk_widget_add_css_class(search_pill, "search-pill");
    GtkWidget *pbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(pbox, 18);
    gtk_widget_set_margin_end(pbox, 18);
    GtkWidget *p_i = gtk_label_new("🔍");
    GtkWidget *p_t = gtk_label_new("Buscar aplicativos…");
    gtk_widget_add_css_class(p_t, "search-pill-txt");
    gtk_widget_set_hexpand(p_t, TRUE);
    gtk_widget_set_halign(p_t, GTK_ALIGN_START);
    GtkWidget *p_e = gtk_label_new("⚡");
    gtk_box_append(GTK_BOX(pbox), p_i);
    gtk_box_append(GTK_BOX(pbox), p_t);
    gtk_box_append(GTK_BOX(pbox), p_e);
    gtk_button_set_child(GTK_BUTTON(search_pill), pbox);
    g_signal_connect_swapped(search_pill, "clicked", G_CALLBACK(switch_to_page), (gpointer)"drawer");
    gtk_box_append(GTK_BOX(page_home), search_pill);

    GtkWidget *hgrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(hgrid), 20);
    gtk_grid_set_column_spacing(GTK_GRID(hgrid), 20);
    gtk_grid_set_row_homogeneous(GTK_GRID(hgrid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(hgrid), TRUE);
    gtk_widget_set_vexpand(hgrid, TRUE);

    gtk_grid_attach(GTK_GRID(hgrid),
        create_app_tile("🎵", "Música", "Player MP3 Hi-Fi", "tile-music", G_CALLBACK(on_launch_music), NULL), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(hgrid),
        create_app_tile("⚙️", "Configurações", "Wi-Fi, Som & Tela", "tile-settings", G_CALLBACK(switch_to_page), (gpointer)"shade"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(hgrid),
        create_app_tile("💻", "Terminal", "Shell Bash ARM64", "tile-terminal", G_CALLBACK(on_launch_terminal), NULL), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(hgrid),
        create_app_tile("⏹", "Recentes", "Tarefas Abertas", "tile-storage", G_CALLBACK(switch_to_page), (gpointer)"recents"), 1, 1, 1, 1);
    gtk_box_append(GTK_BOX(page_home), hgrid);

    /* Dock Inferior */
    GtkWidget *dock_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(dock_box, GTK_ALIGN_CENTER);
    GtkWidget *dock = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(dock, "app-dock");

    gtk_box_append(GTK_BOX(dock), create_dock_btn("💻", "Terminal", G_CALLBACK(on_launch_terminal), NULL, FALSE));
    gtk_box_append(GTK_BOX(dock), create_dock_btn("🎵", "Música", G_CALLBACK(on_launch_music), NULL, FALSE));
    gtk_box_append(GTK_BOX(dock), create_dock_btn("⊞", "Gaveta de Apps", G_CALLBACK(on_toggle_drawer), NULL, TRUE));
    gtk_box_append(GTK_BOX(dock), create_dock_btn("⏹", "Recentes", G_CALLBACK(switch_to_page), (gpointer)"recents", FALSE));
    gtk_box_append(GTK_BOX(dock), create_dock_btn("⚙️", "Ajustes", G_CALLBACK(switch_to_page), (gpointer)"shade", FALSE));
    gtk_box_append(GTK_BOX(dock_box), dock);
    gtk_box_append(GTK_BOX(page_home), dock_box);

    gtk_stack_add_named(GTK_STACK(stack), page_home, "home");

    /* PÁGINA: "drawer" (Módulo do Agente 1 - BF) */
    GtkWidget *page_drawer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page_drawer, 28);
    gtk_widget_set_margin_end(page_drawer, 28);
    gtk_widget_set_margin_top(page_drawer, 20);
    gtk_widget_set_margin_bottom(page_drawer, 16);

    GtkWidget *dr_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *dr_t = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(dr_t), "<span font='24' weight='bold'>⊞ Gaveta de Aplicativos</span>");
    gtk_widget_set_hexpand(dr_t, TRUE);
    gtk_widget_set_halign(dr_t, GTK_ALIGN_START);
    GtkWidget *dr_c = gtk_button_new_with_label("✕");
    gtk_widget_add_css_class(dr_c, "btn-circle-close");
    g_signal_connect_swapped(dr_c, "clicked", G_CALLBACK(switch_to_page), (gpointer)"home");
    gtk_box_append(GTK_BOX(dr_hdr), dr_t);
    gtk_box_append(GTK_BOX(dr_hdr), dr_c);

    GtkWidget *drawer_widget = app_drawer_create(on_drawer_app_launch);

    /* Botão do teclado virtual touch (módulo app_drawer / Fase 2).
       Chamado DEPOIS de app_drawer_create p/ reaproveitar o mesmo OSK. */
    GtkWidget *osk_w = keyboard_create_popup(NULL);
    GtkWidget *dr_k = gtk_button_new_with_label("⌨");
    gtk_widget_add_css_class(dr_k, "btn-circle-close");
    g_signal_connect(dr_k, "clicked", G_CALLBACK(on_osk_toggle), osk_w);
    gtk_box_append(GTK_BOX(dr_hdr), dr_k);

    gtk_box_append(GTK_BOX(page_drawer), dr_hdr);
    gtk_box_append(GTK_BOX(page_drawer), drawer_widget);
    gtk_stack_add_named(GTK_STACK(stack), page_drawer, "drawer");

    /* PÁGINA: "shade" (Quick Settings) */
    GtkWidget *page_shade = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page_shade, 28);
    gtk_widget_set_margin_end(page_shade, 28);
    gtk_widget_set_margin_top(page_shade, 20);
    gtk_widget_set_margin_bottom(page_shade, 16);

    GtkWidget *sh_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *sh_t = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(sh_t), "<span font='24' weight='bold'>⚡ Configurações Rápidas</span>");
    gtk_widget_set_hexpand(sh_t, TRUE);
    gtk_widget_set_halign(sh_t, GTK_ALIGN_START);
    GtkWidget *sh_c = gtk_button_new_with_label("✕");
    gtk_widget_add_css_class(sh_c, "btn-circle-close");
    g_signal_connect_swapped(sh_c, "clicked", G_CALLBACK(switch_to_page), (gpointer)"home");
    gtk_box_append(GTK_BOX(sh_hdr), sh_t);
    gtk_box_append(GTK_BOX(sh_hdr), sh_c);
    gtk_box_append(GTK_BOX(page_shade), sh_hdr);

    GtkWidget *sgrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(sgrid), 16);
    gtk_grid_set_column_spacing(GTK_GRID(sgrid), 16);
    gtk_grid_set_row_homogeneous(GTK_GRID(sgrid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(sgrid), TRUE);

    app.btn_shade_wifi = create_shade_btn("📶", "Wi-Fi", &app.lbl_shade_wifi_sub, G_CALLBACK(on_toggle_wifi));
    app.btn_shade_bt   = create_shade_btn("ᛒ", "Bluetooth", &app.lbl_shade_bt_sub, G_CALLBACK(on_toggle_bt));
    app.btn_shade_air  = create_shade_btn("✈", "Modo Avião", NULL, G_CALLBACK(on_toggle_airplane));
    GtkWidget *b_slp   = create_shade_btn("🌙", "Apagar Tela", NULL, G_CALLBACK(enter_sleep_lock));

    gtk_grid_attach(GTK_GRID(sgrid), app.btn_shade_wifi, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(sgrid), app.btn_shade_bt,   1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(sgrid), app.btn_shade_air,  0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(sgrid), b_slp,              1, 1, 1, 1);
    gtk_box_append(GTK_BOX(page_shade), sgrid);

    GtkWidget *s_bl = create_slider_row("☀", "Brilho da Tela", backlight_get(), G_CALLBACK(on_shade_bl_changed));
    app.scale_shade_bl = gtk_widget_get_last_child(s_bl);
    gtk_box_append(GTK_BOX(page_shade), s_bl);

    GtkWidget *s_vl = create_slider_row("🔊", "Volume do Alto-falante", volume_get(), G_CALLBACK(on_shade_vol_changed));
    app.scale_shade_vol = gtk_widget_get_last_child(s_vl);
    gtk_box_append(GTK_BOX(page_shade), s_vl);

    GtkWidget *sh_acts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget *b_reb = gtk_button_new_with_label("⟳ Reiniciar");
    gtk_widget_add_css_class(b_reb, "btn-power-reboot");
    gtk_widget_set_hexpand(b_reb, TRUE);
    gtk_widget_set_size_request(b_reb, -1, 56);
    g_signal_connect(b_reb, "clicked", G_CALLBACK(on_power_reboot), NULL);

    GtkWidget *b_off = gtk_button_new_with_label("⏻ Desligar");
    gtk_widget_add_css_class(b_off, "btn-power-off");
    gtk_widget_set_hexpand(b_off, TRUE);
    gtk_widget_set_size_request(b_off, -1, 56);
    g_signal_connect(b_off, "clicked", G_CALLBACK(on_power_off), NULL);

    gtk_box_append(GTK_BOX(sh_acts), b_reb);
    gtk_box_append(GTK_BOX(sh_acts), b_off);
    gtk_box_append(GTK_BOX(page_shade), sh_acts);
    gtk_stack_add_named(GTK_STACK(stack), page_shade, "shade");

    /* PÁGINA: "recents" (Módulo do Agente 2 - Antigravity / Fase 3) */
    GtkWidget *page_recents = wm_create_task_switcher_view(on_return_home_cb);
    gtk_stack_add_named(GTK_STACK(stack), page_recents, "recents");

    /* PÁGINA: "lock" */
    GtkWidget *page_lock = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_valign(page_lock, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(page_lock, GTK_ALIGN_CENTER);

    app.lbl_lock_time = gtk_label_new(NULL);
    app.lbl_lock_date = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(page_lock), app.lbl_lock_time);
    gtk_box_append(GTK_BOX(page_lock), app.lbl_lock_date);

    GtkWidget *btn_unlock = gtk_button_new();
    gtk_widget_add_css_class(btn_unlock, "btn-unlock");
    gtk_widget_set_size_request(btn_unlock, 320, 80);
    GtkWidget *ubox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(ubox, GTK_ALIGN_CENTER);
    GtkWidget *u_i = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(u_i), "<span font='32'>🔓</span>");
    GtkWidget *u_t = gtk_label_new("Toque para Desbloquear");
    gtk_widget_add_css_class(u_t, "btn-unlock-txt");
    gtk_box_append(GTK_BOX(ubox), u_i);
    gtk_box_append(GTK_BOX(ubox), u_t);
    gtk_button_set_child(GTK_BUTTON(btn_unlock), ubox);
    g_signal_connect(btn_unlock, "clicked", G_CALLBACK(unlock_screen), NULL);
    gtk_box_append(GTK_BOX(page_lock), btn_unlock);
    gtk_stack_add_named(GTK_STACK(stack), page_lock, "lock");

    /* 3. Barra de Navegação Android 3-Button */
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

    /* CSS */
    const char *css =
        "window { background-color: #0b0e14; }\n"
        ".root-canvas { background: linear-gradient(180deg, #090b10 0%, #111622 45%, #0d111a 100%); }\n"
        ".status-btn-overlay { background: transparent; border: none; padding: 0; margin: 0; }\n"
        ".status-bar { background: rgba(11, 14, 20, 0.85); padding: 8px 24px; border-bottom: 1px solid rgba(255, 255, 255, 0.08); }\n"
        ".status-time { color: #f0f3f8; font-size: 17px; font-weight: bold; }\n"
        ".status-item { color: #b0b8c6; font-size: 14px; font-weight: 500; margin-left: 8px; }\n"
        ".at-a-glance-card { padding: 12px 24px; }\n"
        ".chip-badge { background: rgba(33, 150, 243, 0.15); border: 1px solid rgba(33, 150, 243, 0.35); border-radius: 20px; padding: 6px 16px; }\n"
        ".search-pill { background: rgba(255, 255, 255, 0.08); border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 28px; min-height: 54px; padding: 4px; }\n"
        ".search-pill:active { background: rgba(255, 255, 255, 0.16); border-color: #2196f3; }\n"
        ".search-pill-txt { color: #9aa3b2; font-size: 15px; }\n"
        ".app-tile { background: #171c26; border: 2px solid #232a38; border-radius: 24px; padding: 16px; min-height: 120px; transition: all 150ms ease-out; }\n"
        ".app-tile:active { transform: scale(0.96); }\n"
        ".tile-music { background: radial-gradient(circle at top, #2b173a 0%, #161a24 100%); border-color: #ab47bc; }\n"
        ".tile-settings { background: radial-gradient(circle at top, #14283b 0%, #161a24 100%); border-color: #2196f3; }\n"
        ".tile-terminal { background: radial-gradient(circle at top, #142e22 0%, #161a24 100%); border-color: #4caf50; }\n"
        ".tile-storage { background: radial-gradient(circle at top, #362413 0%, #161a24 100%); border-color: #ff9800; }\n"
        ".tile-info { background: radial-gradient(circle at top, #132d36 0%, #161a24 100%); border-color: #00bcd4; }\n"
        ".app-tile-name { color: #f0f3f8; font-size: 18px; font-weight: bold; margin-top: 4px; }\n"
        ".app-tile-desc { color: #8e99a8; font-size: 12px; }\n"
        ".app-dock { background: rgba(23, 28, 38, 0.88); border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 44px; padding: 10px 20px; }\n"
        ".dock-btn { background: #202735; border: 1px solid #2d3648; border-radius: 28px; min-width: 60px; min-height: 60px; padding: 0; }\n"
        ".dock-btn:active { background: #2196f3; }\n"
        ".dock-btn-center { background: #1976d2; border: 2px solid #64b5f6; border-radius: 34px; min-width: 68px; min-height: 68px; padding: 0; }\n"
        ".dock-btn-center:active { background: #0d47a1; }\n"
        ".shade-tile { background: #1a202c; border: 2px solid #283142; border-radius: 20px; min-height: 110px; padding: 12px; }\n"
        ".shade-tile.tile-active { background: #1565c0; border-color: #64b5f6; }\n"
        ".shade-tile-title { color: #ffffff; font-size: 17px; font-weight: bold; }\n"
        ".shade-tile-sub { color: #b0bec5; font-size: 13px; }\n"
        ".slider-card { background: #161c26; border: 1px solid #252d3d; border-radius: 18px; padding: 14px 18px; }\n"
        ".slider-label { color: #cfd8dc; font-size: 15px; font-weight: 500; }\n"
        "scale trough { background: #263238; border-radius: 6px; min-height: 12px; }\n"
        "scale highlight { background: #2196f3; border-radius: 6px; }\n"
        "scale slider { background: #ffffff; border-radius: 14px; min-width: 28px; min-height: 28px; }\n"
        ".btn-circle-close { background: rgba(255, 255, 255, 0.12); color: #ffffff; border-radius: 20px; min-width: 40px; min-height: 40px; font-size: 18px; border: none; }\n"
        ".btn-unlock { background: #1976d2; border: 2px solid #64b5f6; border-radius: 36px; color: #ffffff; }\n"
        ".btn-unlock-txt { font-size: 20px; font-weight: bold; color: #ffffff; }\n"
        ".android-navbar { background: #06080c; border-top: 1px solid rgba(255, 255, 255, 0.08); }\n"
        ".navbar-btn { background: transparent; border: none; color: #c0c8d4; font-size: 24px; }\n"
        ".navbar-btn:active { background: rgba(255, 255, 255, 0.12); color: #2196f3; }\n"
        ".task-card { background: #161c26; border: 1px solid #283142; border-radius: 20px; padding: 16px; }\n"
        ".task-card-title { color: #f0f3f8; font-size: 19px; font-weight: bold; }\n"
        ".task-card-meta { color: #9aa3b2; font-size: 13px; }\n"
        ".btn-task-kill { background: #c62828; color: #ffffff; border-radius: 12px; border: none; padding: 6px 14px; font-weight: bold; }\n"
        ".btn-task-resume { background: #1976d2; color: #ffffff; border-radius: 14px; border: none; font-size: 16px; font-weight: bold; margin-top: 6px; }\n"
        ".btn-clear-all { background: #263238; border: 2px solid #37474f; color: #eceff1; border-radius: 18px; font-size: 17px; font-weight: bold; }\n"
        ".btn-clear-all:active { background: #c62828; border-color: #ef5350; }\n"
        ".empty-task-title { color: #eceff1; font-size: 22px; font-weight: bold; }\n"
        ".empty-task-sub { color: #90a4ae; font-size: 15px; }\n"
        ".btn-power-reboot { background: #1976d2; color: #ffffff; border-radius: 14px; font-size: 17px; font-weight: bold; border: none; }\n"
        ".btn-power-off { background: #d32f2f; color: #ffffff; border-radius: 14px; font-size: 17px; font-weight: bold; border: none; }\n"
        /* ---------- Fase 2: Gaveta dinâmica, busca e teclado (app_drawer.c) ---------- */
        ".search-entry { background: rgba(255, 255, 255, 0.08); border: 1px solid rgba(255, 255, 255, 0.14); border-radius: 30px; min-height: 60px; padding: 0 20px; color: #f0f3f8; font-size: 17px; caret-color: #2196f3; }\n"
        ".search-entry:focus { border-color: #2196f3; background: rgba(255, 255, 255, 0.13); }\n"
        ".search-entry text { color: #f0f3f8; }\n"
        ".search-entry text > placeholder { color: #8e99a8; font-size: 16px; }\n"
        ".search-entry image { color: #9aa3b2; }\n"
        ".drawer-tile { min-height: 148px; }\n"
        ".tile-power { background: radial-gradient(circle at top, #361717 0%, #161a24 100%); border-color: #ef5350; }\n"
        ".osk { background: rgba(15, 19, 27, 0.98); border-top: 1px solid rgba(255, 255, 255, 0.10); border-radius: 24px 24px 0 0; }\n"
        ".osk-key { background: #2a3140; border: 1px solid #3a4354; border-radius: 14px; color: #f0f3f8; min-height: 68px; min-width: 68px; padding: 0 6px; transition: all 100ms ease-out; }\n"
        ".osk-key:active { background: #2f6fb2; border-color: #64b5f6; }\n"
        ".osk-key-lbl { color: #f0f3f8; font-size: 21px; font-weight: 500; }\n"
        ".osk-key-wide { min-width: 104px; background: #1f2530; border-color: #2d3648; }\n"
        ".osk-shift-on { background: #1976d2; border-color: #64b5f6; }\n"
        ".osk-key-accent { background: #1976d2; border-color: #42a5f5; }\n"
        ".osk-key-accent:active { background: #0d47a1; }\n"
        ".osk-key-space { min-width: 120px; }\n";

    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(prov, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

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
    GtkApplication *gtk_app = gtk_application_new("br.sanders.androidlauncher", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), NULL);
    int rc = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return rc;
}
