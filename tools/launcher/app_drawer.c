/*
 * app_drawer.c — Gaveta de Aplicativos e Teclado Virtual Touch
 * Módulo da Fase 2 — Agente 1 (BF)
 *
 * Implementação completa do contrato em app_drawer.h:
 *   - Descoberta dinâmica de apps via parser de arquivos .desktop
 *     (/usr/local/share/applications com prioridade + /usr/share/applications,
 *     dedup por ID XDG), com suporte a Name[pt_BR], Exec, TryExec, Terminal,
 *     filtragem de Type/Hidden/NoDisplay e remoção dos campos %f/%F/%u/%U/%i/%c.
 *   - Lista ordenada alfabeticamente (casefold + NFKD sem acentos).
 *   - Busca em tempo real (GtkSearchEntry) com contador e estado vazio.
 *   - Teclado virtual touch nativo GTK4 (Opção B do plano): QWERTY + camada
 *     numérica/simbólica, Shift one-shot, Backspace, Espaço e Enter, que sobe
 *     automaticamente quando o campo de busca recebe foco.
 *   - Apps com Terminal=true abrem dentro do weston-terminal (padrão validado
 *     no device); os demais são lançados via gtk-launch <ID XDG>.
 *
 * Compilação modular (ver Makefile no mesmo diretório):
 *   make && make install   → /usr/local/bin/sanders-launcher
 */

#include "app_drawer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <dirent.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Estado do módulo                                                   */
/* ------------------------------------------------------------------ */

#define APPS_MAX 64

typedef struct {
    char *name;        /* Nome exibido (Name[pt_BR] > Name) */
    char *desc;        /* Subtítulo (binário do Exec) */
    char *key;         /* Nome normalizado p/ busca (lowercase, sem acentos) */
    char *cmd;         /* Comando passado ao callback de launch */
    char glyph[12];    /* Ícone emoji por heurística */
    const char *accent;/* Classe CSS de cor do card */
} DrawerApp;

static DrawerApp drawer_apps[APPS_MAX];
static int drawer_n_apps = 0;

static AppLaunchCallback launch_cb = NULL;

/* UI da gaveta */
static GtkWidget *dr_search;   /* GtkSearchEntry */
static GtkWidget *dr_scroll;   /* GtkScrolledWindow com a grade dinâmica */
static GtkWidget *dr_empty;    /* Label "nenhum resultado" */
static GtkWidget *lbl_count;   /* Contador "N apps" */
static gpointer first_result;  /* Primeiro resultado filtrado (Enter lança) */

/* Estado do teclado virtual */
static GtkWidget *osk_reveal;
static GtkWidget *osk_btn_shift;
static GtkWidget *osk_letters_layer;
static GtkWidget *osk_syms_layer;
static GtkWidget *osk_letter_btns[26];
static gboolean osk_shift_on;
static GtkEditable *osk_target = NULL; /* Campo de texto alimentado pelo OSK */

/* ------------------------------------------------------------------ */
/* Utilitários                                                        */
/* ------------------------------------------------------------------ */

/* Normaliza p/ busca: casefold + NFKD + remove marcas de combinação */
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
            continue;
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
    if (strstr(lname, "energ") || strstr(lname, "power")) return "⏻";
    if (strstr(lname, "fastfetch") || strstr(lname, "info")) return "🚀";
    return "📦";
}

/* Cor de card estável por hash do nome */
static const char *pick_accent(const char *name)
{
    static const char *accents[] = { "tile-music", "tile-settings", "tile-terminal",
                                     "tile-storage", "tile-info", "tile-power" };
    unsigned h = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        h = h * 31u + *p;
    return accents[h % G_N_ELEMENTS(accents)];
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

static int drawer_cmp_name(const void *pa, const void *pb)
{
    const DrawerApp *a = pa, *b = pb;
    return strcmp(a->key, b->key);
}

/* ------------------------------------------------------------------ */
/* Cadastro de aplicativos                                            */
/* ------------------------------------------------------------------ */

static void add_cmd_app(const char *name, const char *desc, const char *glyph, const char *cmd)
{
    if (drawer_n_apps >= APPS_MAX)
        return;
    DrawerApp *a = &drawer_apps[drawer_n_apps++];
    a->name = g_strdup(name);
    a->desc = g_strdup(desc);
    a->key = normalize_key(name);
    a->cmd = g_strdup(cmd);
    snprintf(a->glyph, sizeof(a->glyph), "%s", glyph);
    a->accent = pick_accent(name);
}

/* App de TUI (Terminal=true): roda dentro do weston-terminal com a linha Exec */
static void add_terminal_exec_app(const char *name, const char *desc, const char *glyph,
                                  const char *exec_line)
{
    char esc[640];
    char cmd[896];
    escape_for_bash(exec_line, esc, sizeof(esc));
    snprintf(cmd, sizeof(cmd),
             "weston-terminal --shell='bash -c \"%s; exec bash\"' &", esc);
    add_cmd_app(name, desc, glyph, cmd);
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

    const char *disp = name_l10n[0] ? name_l10n : (name[0] ? name : id);
    char desc[128];
    char first_tok[128] = "";
    sscanf(exec, "%127s", first_tok);
    char *base = strrchr(first_tok, '/');
    snprintf(desc, sizeof(desc), "%s", base ? base + 1 : first_tok);

    if (terminal) {
        add_terminal_exec_app(disp, desc, pick_glyph(disp), exec);
    } else {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "gtk-launch %s &", id);
        add_cmd_app(disp, desc, pick_glyph(disp), cmd);
    }
}

static void load_applications(void)
{
    for (int i = 0; i < drawer_n_apps; i++) {
        g_free(drawer_apps[i].name);
        g_free(drawer_apps[i].desc);
        g_free(drawer_apps[i].key);
        g_free(drawer_apps[i].cmd);
    }
    drawer_n_apps = 0;

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

    /* Ferramentas nativas do shell (cmd direto, rastreadas pelo window_manager) */
    add_cmd_app("Música Hi-Fi", "Player MP3 GTK4", "🎵", "/root/mp3_player/player_gtk4 &");
    add_cmd_app("Terminal", "Shell Bash ARM64", "💻", "weston-terminal &");
    add_cmd_app("Processos (Htop)", "Monitor de processos", "📊",
                "weston-terminal --shell=/usr/bin/htop &");
    add_cmd_app("Fastfetch", "Specs de hardware", "🚀",
                "weston-terminal --shell='bash -c \"fastfetch; exec bash\"' &");

    /* Ordenação alfabética estável */
    qsort(drawer_apps, drawer_n_apps, sizeof(DrawerApp), drawer_cmp_name);
}

/* ------------------------------------------------------------------ */
/* Teclado Virtual Touch (OSK GTK4 — Opção B do plano)                */
/* ------------------------------------------------------------------ */

static void osk_show(void)
{
    if (osk_reveal)
        gtk_revealer_set_reveal_child(GTK_REVEALER(osk_reveal), TRUE);
}

static void osk_hide(void)
{
    if (osk_reveal)
        gtk_revealer_set_reveal_child(GTK_REVEALER(osk_reveal), FALSE);
}

/* Entrada de texto no campo alvo via interface GtkEditable */
static void osk_insert_text(const char *s)
{
    if (!GTK_IS_EDITABLE(osk_target))
        return;
    int pos = gtk_editable_get_position(osk_target);
    gtk_editable_insert_text(osk_target, s, -1, &pos);
    gtk_editable_set_position(osk_target, pos);
}

static void on_key_backspace(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    if (!GTK_IS_EDITABLE(osk_target))
        return;
    int pos = gtk_editable_get_position(osk_target);
    if (pos > 0)
        gtk_editable_delete_text(osk_target, pos - 1, pos);
}

static void on_key_letter(GtkWidget *w G_GNUC_UNUSED, gpointer d)
{
    int code = GPOINTER_TO_INT(d); /* 'a'..'z' */
    char ch = osk_shift_on ? (char)(code - 32) : (char)code;
    char s[2] = { ch, '\0' };
    osk_insert_text(s);
    /* Shift é one-shot, como no Android */
    if (osk_shift_on) {
        osk_shift_on = FALSE;
        if (osk_btn_shift)
            gtk_widget_remove_css_class(osk_btn_shift, "osk-shift-on");
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
    osk_shift_on = !osk_shift_on;
    if (osk_btn_shift) {
        if (osk_shift_on)
            gtk_widget_add_css_class(osk_btn_shift, "osk-shift-on");
        else
            gtk_widget_remove_css_class(osk_btn_shift, "osk-shift-on");
    }
    /* Atualiza a legenda das teclas de A..Z */
    for (int i = 0; i < 26; i++) {
        if (!osk_letter_btns[i])
            continue;
        GtkWidget *lbl = gtk_button_get_child(GTK_BUTTON(osk_letter_btns[i]));
        if (GTK_IS_LABEL(lbl)) {
            char c = osk_shift_on ? (char)('A' + i) : (char)('a' + i);
            char m[64];
            snprintf(m, sizeof(m), "<span font='22' weight='500'>%c</span>", c);
            gtk_label_set_markup(GTK_LABEL(lbl), m);
        }
    }
}

static void on_key_toggle_layer(GtkWidget *w G_GNUC_UNUSED, gpointer d)
{
    gboolean show_symbols = GPOINTER_TO_INT(d) != 0;
    if (osk_letters_layer)
        gtk_widget_set_visible(osk_letters_layer, !show_symbols);
    if (osk_syms_layer)
        gtk_widget_set_visible(osk_syms_layer, show_symbols);
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
        gtk_widget_set_focusable(btn, FALSE); /* não rouba foco do campo */

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
                osk_letter_btns[idx] = btn;
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

static GtkWidget *build_osk(GtkEditable *target)
{
    osk_target = target;
    osk_shift_on = FALSE;

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
    osk_letters_layer = letters;

    GtkWidget *r1 = make_osk_row("qwertyuiop", TRUE);
    gtk_widget_set_halign(r1, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(letters), r1);

    GtkWidget *r2 = make_osk_row("asdfghjkl", TRUE);
    gtk_widget_set_halign(r2, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(letters), r2);

    GtkWidget *row3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(row3, GTK_ALIGN_CENTER);

    osk_btn_shift = make_osk_button("<span font='20'>⇧</span>", "osk-key-wide",
                                    G_CALLBACK(on_key_shift), NULL);
    gtk_box_append(GTK_BOX(row3), osk_btn_shift);

    gtk_box_append(GTK_BOX(row3), make_osk_row("zxcvbnm", TRUE));

    gtk_box_append(GTK_BOX(row3), make_osk_button("<span font='20'>⌫</span>", "osk-key-wide",
                                                  G_CALLBACK(on_key_backspace), NULL));
    gtk_box_append(GTK_BOX(letters), row3);
    gtk_box_append(GTK_BOX(kb), letters);

    /* ------- Camada NUMÉRICA/SÍMBOLOS ------- */
    GtkWidget *syms = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_visible(syms, FALSE);
    osk_syms_layer = syms;

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

    GtkWidget *btn_syms = make_osk_button("<span font='18' weight='bold'>?123</span>",
                                          "osk-key-wide", G_CALLBACK(on_key_toggle_layer),
                                          GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(bottom), btn_syms);

    GtkWidget *btn_abc = make_osk_button("<span font='18' weight='bold'>ABC</span>",
                                         "osk-key-wide", G_CALLBACK(on_key_toggle_layer),
                                         GINT_TO_POINTER(0));
    gtk_widget_set_visible(btn_abc, FALSE);
    gtk_box_append(GTK_BOX(bottom), btn_abc);

    GtkWidget *space = make_osk_button("<span font='18'>espaço</span>", "osk-key-space",
                                       G_CALLBACK(on_key_space), NULL);
    gtk_widget_set_hexpand(space, TRUE);
    gtk_box_append(GTK_BOX(bottom), space);

    gtk_box_append(GTK_BOX(bottom), make_osk_button("<span font='20'>⏎</span>", "osk-key-accent",
                                                    G_CALLBACK(osk_hide), NULL));

    gtk_box_append(GTK_BOX(kb), bottom);

    osk_reveal = rev;
    return rev;
}

/* ------------------------------------------------------------------ */
/* Gaveta dinâmica (grade + busca)                                    */
/* ------------------------------------------------------------------ */

static void on_drawer_tile_clicked(GtkWidget *w G_GNUC_UNUSED, gpointer d)
{
    DrawerApp *a = (DrawerApp *)d;
    if (launch_cb && a && a->cmd)
        launch_cb(a->cmd);
}

static void rebuild_drawer_grid(void)
{
    const char *q_raw = gtk_editable_get_text(GTK_EDITABLE(dr_search));
    char *q = normalize_key(q_raw);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

    int count = 0;
    first_result = NULL;
    for (int i = 0; i < drawer_n_apps; i++) {
        if (q[0] && !strstr(drawer_apps[i].key, q))
            continue;
        if (!first_result)
            first_result = &drawer_apps[i];

        GtkWidget *btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "app-tile");
        gtk_widget_add_css_class(btn, drawer_apps[i].accent);
        gtk_widget_add_css_class(btn, "drawer-tile");

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(box, GTK_ALIGN_CENTER);

        GtkWidget *lbl_i = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl_i),
            g_markup_printf_escaped("<span font='38'>%s</span>", drawer_apps[i].glyph));

        GtkWidget *lbl_n = gtk_label_new(drawer_apps[i].name);
        gtk_widget_add_css_class(lbl_n, "app-tile-name");
        gtk_label_set_ellipsize(GTK_LABEL(lbl_n), PANGO_ELLIPSIZE_END);
        gtk_widget_set_size_request(lbl_n, 140, -1);

        GtkWidget *lbl_d = gtk_label_new(drawer_apps[i].desc);
        gtk_widget_add_css_class(lbl_d, "app-tile-desc");
        gtk_label_set_ellipsize(GTK_LABEL(lbl_d), PANGO_ELLIPSIZE_END);
        gtk_widget_set_size_request(lbl_d, 140, -1);

        gtk_box_append(GTK_BOX(box), lbl_i);
        gtk_box_append(GTK_BOX(box), lbl_n);
        gtk_box_append(GTK_BOX(box), lbl_d);
        gtk_button_set_child(GTK_BUTTON(btn), box);

        g_signal_connect(btn, "clicked", G_CALLBACK(on_drawer_tile_clicked), &drawer_apps[i]);
        gtk_grid_attach(GTK_GRID(grid), btn, count % 3, count / 3, 1, 1);
        count++;
    }

    char cnt[80];
    snprintf(cnt, sizeof(cnt), "<span font='15' color='#8e99a8'>%d apps</span>", count);
    gtk_label_set_markup(GTK_LABEL(lbl_count), cnt);
    gtk_widget_set_visible(dr_empty, count == 0);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(dr_scroll), grid);
    g_free(q);
}

static void on_search_changed(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    rebuild_drawer_grid();
}

static void on_search_activate(GtkWidget *w G_GNUC_UNUSED, gpointer d G_GNUC_UNUSED)
{
    on_drawer_tile_clicked(NULL, first_result);
}

static void on_entry_focus_changed(GObject *obj, GParamSpec *ps G_GNUC_UNUSED,
                                   gpointer d G_GNUC_UNUSED)
{
    if (gtk_widget_has_focus(GTK_WIDGET(obj)))
        osk_show();
}

/* ------------------------------------------------------------------ */
/* API pública (contrato em app_drawer.h)                             */
/* ------------------------------------------------------------------ */

GtkWidget *app_drawer_create(AppLaunchCallback on_launch)
{
    launch_cb = on_launch;
    load_applications();

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);

    /* Campo de busca em tempo real */
    dr_search = gtk_search_entry_new();
    gtk_widget_add_css_class(dr_search, "search-entry");
    gtk_widget_set_size_request(dr_search, -1, 60);
    g_signal_connect(dr_search, "search-changed", G_CALLBACK(on_search_changed), NULL);
    g_signal_connect(dr_search, "activate", G_CALLBACK(on_search_activate), NULL);
    gtk_box_append(GTK_BOX(box), dr_search);

    /* Placeholder + foco no GtkText interno → sobe o teclado virtual */
    GtkWidget *search_text = gtk_widget_get_first_child(dr_search);
    if (GTK_IS_TEXT(search_text)) {
        gtk_text_set_placeholder_text(GTK_TEXT(search_text), "Buscar aplicativos…");
        g_signal_connect(search_text, "notify::has-focus",
                         G_CALLBACK(on_entry_focus_changed), NULL);
    }

    /* Contador + estado vazio */
    lbl_count = gtk_label_new(NULL);
    gtk_widget_set_halign(lbl_count, GTK_ALIGN_END);

    dr_empty = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(dr_empty),
        "<span font='20' color='#8e99a8'>🔍 Nenhum aplicativo encontrado\n"
        "<span font='15'>Tente outro termo de busca</span></span>");
    gtk_label_set_justify(GTK_LABEL(dr_empty), GTK_JUSTIFY_CENTER);
    gtk_widget_set_margin_top(dr_empty, 60);
    gtk_widget_set_visible(dr_empty, FALSE);

    GtkWidget *count_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(count_row), lbl_count);
    gtk_box_append(GTK_BOX(box), count_row);

    /* Grade dinâmica rolável (3 colunas, ordenada A→Z) */
    dr_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(dr_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(dr_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_append(GTK_BOX(box), dr_scroll);

    /* Teclado virtual touch (sobe quando o campo ganha foco) */
    gtk_box_append(GTK_BOX(box), build_osk(GTK_EDITABLE(dr_search)));

    rebuild_drawer_grid();
    return box;
}

void app_drawer_refresh_list(void)
{
    load_applications();
    if (dr_scroll)
        rebuild_drawer_grid();
}

/*
 * Retorna o widget do teclado virtual. Se target_entry for fornecido (e for um
 * GtkEditable), o teclado passa a alimentar esse campo; sem argumento, alimenta
 * o campo de busca da própria gaveta. O teclado da gaveta sobe automaticamente
 * quando o campo ganha foco; para exibi-lo manualmente, adicione o widget
 * retornado ao layout e chame gtk_revealer_set_reveal_child(TRUE).
 */
GtkWidget *keyboard_create_popup(GtkWidget *target_entry)
{
    if (target_entry && GTK_IS_EDITABLE(target_entry))
        osk_target = GTK_EDITABLE(target_entry);
    if (!osk_reveal) {
        /* Gaveta ainda não criada: constrói OSK standalone ligado ao alvo */
        return build_osk(GTK_IS_EDITABLE(target_entry) ? GTK_EDITABLE(target_entry) : NULL);
    }
    return osk_reveal;
}
