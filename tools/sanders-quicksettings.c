/*
 * sanders-quicksettings.c — Painel de configurações rápidas estilo Android
 * Projeto sanders-linux-mainline (Moto G5 Plus, Arch ARM64, Weston 15)
 *
 * Compilação no próprio device (padrão do player GTK4):
 *   gcc -O2 -Wall -o /usr/local/bin/sanders-quicksettings \
 *       tools/sanders-quicksettings.c $(pkg-config --cflags --libs gtk4)
 *
 * Execução (herda o Wayland do Weston, que roda como root):
 *   XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-1 sanders-quicksettings
 *
 * Backends controlados (todos validados ao vivo em 2026-09-10, BF):
 *   - Wi-Fi / Bluetooth / Avião: sysfs rfkill (/sys/class/rfkill/<id>/soft, por nome)
 *   - Brilho: /sys/class/backlight/<card>/{brightness,max_brightness}
 *   - Volume: amixer -c 0 sset "RX1 Digital"|"RX2 Digital" N% (codec QDSP6)
 *   - Status Wi-Fi: wpa_cli -i wlan0 status (SSID conectado)
 *
 * BF (agente Codebuff) — doc: docs/STATUSBAR_QUICKSETTINGS_PLAN.md
 */
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>

/* ------------------------------------------------------------------ */
/* util: comando -> primeira linha de stdout (SEM formatacao: string    */
/* pura, para nao sofrer mangling de '%' pelo vsnprintf)               */
static char *run_get_line(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return NULL;
    static char buf[512];
    buf[0] = '\0';
    if (fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\n")] = '\0';
    } else {
        buf[0] = '\0';
    }
    pclose(fp);
    return buf;
}

/* execvp de argv terminado em NULL — sem shell, sem quoting */
static void run_argv(char *const argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execvp(argv[0], argv);
        _exit(127);
    }
    if (pid > 0)
        waitpid(pid, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* rfkill por nome de dispositivo (não confiar no índice)              */
static int rfkill_find(const char *devname, char *out, size_t outlen)
{
    DIR *d = opendir("/sys/class/rfkill");
    if (!d)
        return -1;
    struct dirent *e;
    int found = -1;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char namepath[256], name[128];
        snprintf(namepath, sizeof(namepath), "/sys/class/rfkill/%s/name",
                 e->d_name);
        FILE *f = fopen(namepath, "r");
        if (!f)
            continue;
        if (fgets(name, sizeof(name), f)) {
            name[strcspn(name, "\n")] = '\0';
            if (strcmp(name, devname) == 0) {
                snprintf(out, outlen, "/sys/class/rfkill/%s", e->d_name);
                found = 0;
            }
        }
        fclose(f);
        if (found == 0)
            break;
    }
    closedir(d);
    return found;
}

/* retorna 1 = desbloqueado (on), 0 = bloqueado (off), -1 = erro */
static int rfkill_state(const char *devname)
{
    char path[256];
    if (rfkill_find(devname, path, sizeof(path)) < 0)
        return -1;
    char softpath[300];
    snprintf(softpath, sizeof(softpath), "%s/soft", path);
    FILE *f = fopen(softpath, "r");
    if (!f)
        return -1;
    int v = fgetc(f);
    fclose(f);
    return (v == '0'); /* "0" = not blocked = on */
}

static void rfkill_set(const char *devname, int on)
{
    char path[256];
    if (rfkill_find(devname, path, sizeof(path)) < 0)
        return;
    char softpath[300];
    snprintf(softpath, sizeof(softpath), "%s/soft", path);
    FILE *f = fopen(softpath, "w");
    if (f) {
        fputc(on ? '0' : '1', f);
        fclose(f);
    }
}

/* ------------------------------------------------------------------ */
/* backlight                                                           */
static char bl_brightness[128] = "";
static int bl_max = 4095;

static void backlight_init(void)
{
    DIR *d = opendir("/sys/class/backlight");
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        snprintf(bl_brightness, sizeof(bl_brightness),
                 "/sys/class/backlight/%s", e->d_name);
        break;
    }
    closedir(d);
    if (!bl_brightness[0])
        return;
    char p[256];
    snprintf(p, sizeof(p), "%s/max_brightness", bl_brightness);
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
    if (!bl_brightness[0])
        return 0.5;
    char p[256];
    snprintf(p, sizeof(p), "%s/brightness", bl_brightness);
    FILE *f = fopen(p, "r");
    if (!f)
        return 0.5;
    int v = 0;
    if (fscanf(f, "%d", &v) != 1)
        v = 0;
    fclose(f);
    return (double)v / (double)bl_max;
}

static void backlight_set(double frac)
{
    if (!bl_brightness[0])
        return;
    int v = (int)(frac * (double)bl_max);
    char p[256];
    snprintf(p, sizeof(p), "%s/brightness", bl_brightness);
    FILE *f = fopen(p, "w");
    if (f) {
        fprintf(f, "%d", v);
        fclose(f);
    }
}

/* ------------------------------------------------------------------ */
/* volume (codec digital gains RX1+RX2)                                */
static double volume_get(void)
{
    char *line = run_get_line(
        "amixer -c 0 sget 'RX1 Digital' 2>/dev/null"
        " | grep -oE '\\[[0-9]+%\\]' | tr -d '[]%'");
    if (!line || !line[0])
        return 0.75;
    return atoi(line) / 100.0;
}

static void volume_set(double frac)
{
    int pct = (int)(frac * 100.0 + 0.5);
    char pctstr[16];
    snprintf(pctstr, sizeof(pctstr), "%d%%", pct);
    char *a1[] = { "amixer", "-c", "0", "sset", "RX1 Digital", pctstr, NULL };
    char *a2[] = { "amixer", "-c", "0", "sset", "RX2 Digital", pctstr, NULL };
    run_argv(a1);
    run_argv(a2);
}

/* ------------------------------------------------------------------ */
/* UI                                                                  */
typedef struct {
    GtkWidget *btn_wifi;
    GtkWidget *btn_bt;
    GtkWidget *btn_air;
    GtkWidget *lbl_wifi;
    GtkWidget *lbl_bt;
    GtkWidget *scale_bl;
    GtkWidget *scale_vol;
    gboolean updating;
} App;

static App app = { .updating = FALSE };

static void tile_set_active(GtkWidget *btn, gboolean active)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context(btn);
    if (active)
        gtk_style_context_add_class(ctx, "active");
    else
        gtk_style_context_remove_class(ctx, "active");
}

static void refresh_status(void)
{
    app.updating = TRUE;

    int w = rfkill_state("phy0");
    int b = rfkill_state("hci0");
    tile_set_active(app.btn_wifi, w == 1);
    tile_set_active(app.btn_bt, b == 1);
    tile_set_active(app.btn_air, w == 0 && b == 0);

    /* SSID atual */
    if (w == 1) {
        char *ssid = run_get_line(
            "wpa_cli -i wlan0 status 2>/dev/null | grep '^ssid=' | cut -d= -f2");
        if (ssid && ssid[0])
            gtk_label_set_text(GTK_LABEL(app.lbl_wifi), ssid);
        else
            gtk_label_set_text(GTK_LABEL(app.lbl_wifi), "sem rede");
    } else {
        gtk_label_set_text(GTK_LABEL(app.lbl_wifi), "desligado");
    }
    gtk_label_set_text(GTK_LABEL(app.lbl_bt),
                       b == 1 ? "ligado" : "desligado");

    if (GTK_IS_RANGE(app.scale_bl))
        gtk_range_set_value(GTK_RANGE(app.scale_bl), backlight_get());
    if (GTK_IS_RANGE(app.scale_vol))
        gtk_range_set_value(GTK_RANGE(app.scale_vol), volume_get());

    app.updating = FALSE;
}

static gboolean refresh_tick(gpointer data G_GNUC_UNUSED)
{
    refresh_status();
    return G_SOURCE_CONTINUE;
}

/* ------------------------- callbacks ------------------------------ */
static void on_wifi(GtkButton *b G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    rfkill_set("phy0", rfkill_state("phy0") != 1);
    refresh_status();
}

static void on_bt(GtkButton *b G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    rfkill_set("hci0", rfkill_state("hci0") != 1);
    refresh_status();
}

static void on_air(GtkButton *b G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    gboolean to_plane = !(rfkill_state("phy0") == 0 && rfkill_state("hci0") == 0);
    rfkill_set("phy0", !to_plane);
    rfkill_set("hci0", !to_plane);
    refresh_status();
}

static void on_reboot(GtkButton *b G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    char *a[] = { "systemctl", "reboot", NULL };
    run_argv(a);
}

static void on_poweroff(GtkButton *b G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    char *a[] = { "systemctl", "poweroff", NULL };
    run_argv(a);
}

static void on_bl_changed(GtkRange *r, gpointer d G_GNUC_UNUSED)
{
    if (!app.updating)
        backlight_set(gtk_range_get_value(r));
}

static void on_vol_changed(GtkRange *r, gpointer d G_GNUC_UNUSED)
{
    if (!app.updating)
        volume_set(gtk_range_get_value(r));
}

/* ------------------------- helpers UI ----------------------------- */
static GtkWidget *make_tile(const char *title, GtkWidget **out_label,
                            GCallback cb)
{
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "tile");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);

    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl), g_markup_printf_escaped(
        "<span font='20' weight='bold'>%s</span>", title));
    GtkWidget *sub = gtk_label_new("…");
    gtk_widget_add_css_class(sub, "dim-label");

    gtk_box_append(GTK_BOX(box), lbl);
    gtk_box_append(GTK_BOX(box), sub);
    gtk_button_set_child(GTK_BUTTON(btn), box);
    if (out_label)
        *out_label = sub;
    g_signal_connect(btn, "clicked", cb, NULL);
    return btn;
}

static GtkWidget *make_slider(const char *title, double val, GCallback cb)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl), g_markup_printf_escaped(
        "<span font='14' weight='bold'>%s</span>", title));
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                0.0, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(scale), val);
    gtk_widget_set_size_request(scale, -1, 44);
    g_signal_connect(scale, "value-changed", cb, NULL);
    gtk_box_append(GTK_BOX(box), lbl);
    gtk_box_append(GTK_BOX(box), scale);
    gtk_widget_add_css_class(box, "sliderbox");
    return box;
}

static void activate(GtkApplication *application, gpointer data G_GNUC_UNUSED)
{
    GtkWidget *win = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(win), "Configurações rápidas");
    gtk_window_set_default_size(GTK_WINDOW(win), 460, 780);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(root, 16);
    gtk_widget_set_margin_bottom(root, 16);
    gtk_widget_set_margin_start(root, 16);
    gtk_widget_set_margin_end(root, 16);
    gtk_window_set_child(GTK_WINDOW(win), root);

    GtkWidget *head = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(head),
        "<span font='18' weight='bold'>⚡ Configurações rápidas</span>");
    gtk_widget_set_halign(head, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(root), head);

    /* linha 1: Wi-Fi | Bluetooth */
    GtkWidget *grid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    app.btn_wifi = make_tile("Wi-Fi", &app.lbl_wifi, G_CALLBACK(on_wifi));
    app.btn_bt = make_tile("Bluetooth", &app.lbl_bt, G_CALLBACK(on_bt));
    gtk_widget_set_hexpand(app.btn_wifi, TRUE);
    gtk_widget_set_hexpand(app.btn_bt, TRUE);
    gtk_widget_set_size_request(app.btn_wifi, -1, 110);
    gtk_widget_set_size_request(app.btn_bt, -1, 110);
    gtk_box_append(GTK_BOX(grid), app.btn_wifi);
    gtk_box_append(GTK_BOX(grid), app.btn_bt);
    gtk_box_append(GTK_BOX(root), grid);

    /* linha 2: Avião | Reiniciar */
    GtkWidget *grid2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *btn_air = make_tile("✈ Avião", NULL, G_CALLBACK(on_air));
    GtkWidget *btn_reb = make_tile("⟳ Reiniciar", NULL, G_CALLBACK(on_reboot));
    gtk_widget_set_hexpand(btn_air, TRUE);
    gtk_widget_set_hexpand(btn_reb, TRUE);
    gtk_widget_set_size_request(btn_air, -1, 110);
    gtk_widget_set_size_request(btn_reb, -1, 110);
    gtk_box_append(GTK_BOX(grid2), btn_air);
    gtk_box_append(GTK_BOX(grid2), btn_reb);
    gtk_box_append(GTK_BOX(root), grid2);
    app.btn_air = btn_air;

    /* linha 3: desligar (full) */
    GtkWidget *btn_off = make_tile("⏻ Desligar", NULL, G_CALLBACK(on_poweroff));
    gtk_widget_set_size_request(btn_off, -1, 84);
    gtk_box_append(GTK_BOX(root), btn_off);

    /* sliders */
    app.scale_bl = NULL;
    GtkWidget *sbox1 = make_slider("☀ Brilho", backlight_get(),
                                   G_CALLBACK(on_bl_changed));
    app.scale_bl = gtk_widget_get_last_child(sbox1);
    gtk_box_append(GTK_BOX(root), sbox1);

    GtkWidget *sbox2 = make_slider("🔊 Volume", volume_get(),
                                   G_CALLBACK(on_vol_changed));
    app.scale_vol = gtk_widget_get_last_child(sbox2);
    gtk_box_append(GTK_BOX(root), sbox2);

    /* CSS dark */
    const char *css =
        "window { background: #101216; }"
        ".tile { background: #1c2026; color: #e8ebf2; border-radius: 14px;"
        "        border: 2px solid #262b33; }"
        ".tile.active { background: #1b4b7a; border-color: #2196f3; }"
        ".tile label { color: #e8ebf2; }"
        ".tile .dim-label { color: #9aa3b2; font-size: 12px; }"
        ".sliderbox { background: #1c2026; border-radius: 14px; padding: 10px;"
        "             border: 2px solid #262b33; }"
        "scale trough { background: #2a3038; border-radius: 6px; min-height: 10px; }"
        "scale highlight { background: #2196f3; border-radius: 6px; }"
        "scale slider { background: #e8ebf2; border-radius: 12px;"
        "               min-width: 24px; min-height: 24px; }";
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(prov, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    refresh_status();
    g_timeout_add_seconds(3, refresh_tick, NULL);

    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv)
{
    backlight_init();
    GtkApplication *appk = gtk_application_new(
        "br.sanders.quicksettings", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(appk, "activate", G_CALLBACK(activate), NULL);
    int rc = g_application_run(G_APPLICATION(appk), argc, argv);
    g_object_unref(appk);
    return rc;
}
