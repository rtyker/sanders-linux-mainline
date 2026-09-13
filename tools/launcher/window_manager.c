/*
 * window_manager.c — Gerenciador de Janelas, Multitarefas e Navegação Android 3-Button
 * Módulo da Fase 3 — Agente Antigravity
 *
 * Implementa:
 *   - Rastreamento em tempo real de PIDs de processos móveis
 *   - Monitoramento de uso de memória via /proc/<pid>/statm
 *   - Finalização graceful (SIGTERM) e limpeza de tarefas
 *   - Ações da barra de navegação 3-button (Home, Back, Recents)
 *   - Visão de Multitarefas estilo Android Recents Overview
 */

#include "window_manager.h"
#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GList *task_list = NULL;
static GtkWidget *current_switcher_box = NULL;
static void (*global_on_return_home)(void) = NULL;

/* ------------------------------------------------------------------ */
/* Util: Leitura de Memória de Processo (/proc/<pid>/statm)          */
/* ------------------------------------------------------------------ */

static double get_process_rss_mb(pid_t pid)
{
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0.0;
    long size = 0, resident = 0;
    if (fscanf(f, "%ld %ld", &size, &resident) == 2) {
        fclose(f);
        long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
        return (double)(resident * page_size_kb) / 1024.0;
    }
    fclose(f);
    return 0.0;
}

static gboolean is_pid_alive(pid_t pid)
{
    if (pid <= 0) return FALSE;
    if (kill(pid, 0) == 0) return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Rastreamento e Limpeza Automática de Tarefas                       */
/* ------------------------------------------------------------------ */

static void refresh_switcher_ui(void);

static gboolean wm_periodic_check(gpointer data G_GNUC_UNUSED)
{
    GList *iter = task_list;
    gboolean changed = FALSE;

    while (iter) {
        AppTask *t = (AppTask *)iter->data;
        GList *next = iter->next;

        int status;
        pid_t res = waitpid(t->pid, &status, WNOHANG);
        if (res > 0 || !is_pid_alive(t->pid)) {
            /* Processo terminou */
            task_list = g_list_delete_link(task_list, iter);
            g_free(t);
            changed = TRUE;
        }
        iter = next;
    }

    if (changed && current_switcher_box) {
        refresh_switcher_ui();
    }

    return G_SOURCE_CONTINUE;
}

void wm_init(void)
{
    g_timeout_add_seconds(2, wm_periodic_check, NULL);
}

void wm_register_launched_app(pid_t pid, const char *name, const char *icon, const char *cmd)
{
    if (pid <= 0) return;

    /* Verifica se já existe na lista */
    GList *iter;
    for (iter = task_list; iter != NULL; iter = iter->next) {
        AppTask *t = (AppTask *)iter->data;
        if (t->pid == pid) {
            t->is_active = TRUE;
            return;
        }
    }

    AppTask *nt = g_new0(AppTask, 1);
    nt->pid = pid;
    g_strlcpy(nt->name, name ? name : "Aplicativo", sizeof(nt->name));
    g_strlcpy(nt->icon, icon ? icon : "📱", sizeof(nt->icon));
    g_strlcpy(nt->cmd, cmd ? cmd : "", sizeof(nt->cmd));
    nt->start_time = time(NULL);
    nt->is_active = TRUE;

    task_list = g_list_prepend(task_list, nt);

    if (current_switcher_box)
        refresh_switcher_ui();
}

void wm_kill_task(pid_t pid)
{
    if (pid <= 1) return;

    kill(pid, SIGTERM);
    usleep(50000); /* 50ms */
    if (is_pid_alive(pid)) {
        kill(pid, SIGKILL);
    }

    GList *iter;
    for (iter = task_list; iter != NULL; iter = iter->next) {
        AppTask *t = (AppTask *)iter->data;
        if (t->pid == pid) {
            task_list = g_list_delete_link(task_list, iter);
            g_free(t);
            break;
        }
    }

    if (current_switcher_box)
        refresh_switcher_ui();
}

void wm_clear_all_tasks(void)
{
    GList *iter = task_list;
    while (iter) {
        AppTask *t = (AppTask *)iter->data;
        GList *next = iter->next;
        if (t->pid > 1) {
            kill(t->pid, SIGTERM);
        }
        g_free(t);
        iter = next;
    }
    g_list_free(task_list);
    task_list = NULL;

    if (current_switcher_box)
        refresh_switcher_ui();
}

GList *wm_get_task_list(void)
{
    return task_list;
}

gboolean wm_has_active_tasks(void)
{
    return (task_list != NULL);
}

/* ------------------------------------------------------------------ */
/* Ações dos Botões da Barra de Navegação Android (3-Button Nav)      */
/* ------------------------------------------------------------------ */

void wm_action_home(GtkWindow *launcher_win)
{
    if (launcher_win) {
        gtk_window_present(launcher_win);
    }
}

void wm_action_back(GtkWindow *launcher_win)
{
    /* Se houver tarefa filha mais recente ativa, fecha suavemente com SIGTERM */
    if (task_list) {
        AppTask *top = (AppTask *)task_list->data;
        if (top && top->pid > 1 && is_pid_alive(top->pid)) {
            kill(top->pid, SIGTERM);
            return;
        }
    }
    if (launcher_win) {
        gtk_window_present(launcher_win);
    }
}

/* ------------------------------------------------------------------ */
/* Alternador de Tarefas / Recentes (Recents Overview)                */
/* ------------------------------------------------------------------ */

static void on_kill_button_clicked(GtkWidget *btn G_GNUC_UNUSED, gpointer data)
{
    pid_t pid = GPOINTER_TO_INT(data);
    wm_kill_task(pid);
}

static void on_clear_all_clicked(GtkWidget *btn G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    wm_clear_all_tasks();
    if (global_on_return_home)
        global_on_return_home();
}

static void on_focus_task_clicked(GtkWidget *btn G_GNUC_UNUSED, gpointer data)
{
    /* Ao focar num aplicativo já aberto */
    pid_t pid = GPOINTER_TO_INT(data);
    /* Envia sinal SIGCONT caso estivesse suspenso */
    if (pid > 1 && is_pid_alive(pid)) {
        kill(pid, SIGCONT);
    }
    if (global_on_return_home)
        global_on_return_home();
}

static void refresh_switcher_ui(void)
{
    if (!current_switcher_box) return;

    /* Limpa filhos existentes */
    GtkWidget *child = gtk_widget_get_first_child(current_switcher_box);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(current_switcher_box), child);
        child = next;
    }

    /* Header */
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *lbl_t = gtk_label_new(NULL);
    int count = g_list_length(task_list);
    char hdr_str[128];
    snprintf(hdr_str, sizeof(hdr_str),
             "<span font='24' weight='bold'>⏹ Aplicativos Abertos (%d)</span>", count);
    gtk_label_set_markup(GTK_LABEL(lbl_t), hdr_str);
    gtk_widget_set_hexpand(lbl_t, TRUE);
    gtk_widget_set_halign(lbl_t, GTK_ALIGN_START);

    GtkWidget *btn_close = gtk_button_new_with_label("✕");
    gtk_widget_add_css_class(btn_close, "btn-circle-close");
    if (global_on_return_home)
        g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(global_on_return_home), NULL);

    gtk_box_append(GTK_BOX(hdr), lbl_t);
    gtk_box_append(GTK_BOX(hdr), btn_close);
    gtk_box_append(GTK_BOX(current_switcher_box), hdr);

    if (count == 0) {
        /* Estado Vazio */
        GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
        gtk_widget_set_vexpand(empty_box, TRUE);
        gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);

        GtkWidget *e_icon = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(e_icon), "<span font='64'>✨</span>");
        GtkWidget *e_msg = gtk_label_new("Nenhum aplicativo recente aberto");
        gtk_widget_add_css_class(e_msg, "empty-task-title");
        GtkWidget *e_sub = gtk_label_new("Sua memória RAM está 100% livre e otimizada.");
        gtk_widget_add_css_class(e_sub, "empty-task-sub");

        gtk_box_append(GTK_BOX(empty_box), e_icon);
        gtk_box_append(GTK_BOX(empty_box), e_msg);
        gtk_box_append(GTK_BOX(empty_box), e_sub);
        gtk_box_append(GTK_BOX(current_switcher_box), empty_box);
        return;
    }

    /* Lista rolável de Cards */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *cards_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(cards_box, 12);
    gtk_widget_set_margin_bottom(cards_box, 12);

    time_t now = time(NULL);
    GList *iter;
    for (iter = task_list; iter != NULL; iter = iter->next) {
        AppTask *t = (AppTask *)iter->data;

        GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_add_css_class(card, "task-card");

        /* Cabeçalho do Card */
        GtkWidget *top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *c_icon = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(c_icon),
            g_markup_printf_escaped("<span font='28'>%s</span>", t->icon));

        GtkWidget *v_title = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(v_title, TRUE);
        GtkWidget *c_name = gtk_label_new(t->name);
        gtk_widget_add_css_class(c_name, "task-card-title");
        gtk_widget_set_halign(c_name, GTK_ALIGN_START);

        double rss = get_process_rss_mb(t->pid);
        long elapsed = (long)(now - t->start_time);
        char meta_str[128];
        snprintf(meta_str, sizeof(meta_str), "PID: %d • RAM: %.1f MB • %ld seg",
                 t->pid, rss, elapsed);
        GtkWidget *c_meta = gtk_label_new(meta_str);
        gtk_widget_add_css_class(c_meta, "task-card-meta");
        gtk_widget_set_halign(c_meta, GTK_ALIGN_START);

        gtk_box_append(GTK_BOX(v_title), c_name);
        gtk_box_append(GTK_BOX(v_title), c_meta);

        GtkWidget *btn_kill = gtk_button_new_with_label("✕ Fechar");
        gtk_widget_add_css_class(btn_kill, "btn-task-kill");
        g_signal_connect(btn_kill, "clicked", G_CALLBACK(on_kill_button_clicked),
                         GINT_TO_POINTER(t->pid));

        gtk_box_append(GTK_BOX(top_row), c_icon);
        gtk_box_append(GTK_BOX(top_row), v_title);
        gtk_box_append(GTK_BOX(top_row), btn_kill);
        gtk_box_append(GTK_BOX(card), top_row);

        /* Botão de Retomar */
        GtkWidget *btn_resume = gtk_button_new_with_label("Abrir Aplicativo");
        gtk_widget_add_css_class(btn_resume, "btn-task-resume");
        gtk_widget_set_size_request(btn_resume, -1, 46);
        g_signal_connect(btn_resume, "clicked", G_CALLBACK(on_focus_task_clicked),
                         GINT_TO_POINTER(t->pid));
        gtk_box_append(GTK_BOX(card), btn_resume);

        gtk_box_append(GTK_BOX(cards_box), card);
    }

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), cards_box);
    gtk_box_append(GTK_BOX(current_switcher_box), scroll);

    /* Botão "Limpar Tudo" estilo Android */
    GtkWidget *btn_clear = gtk_button_new_with_label("🗑️ Limpar Todos os Aplicativos");
    gtk_widget_add_css_class(btn_clear, "btn-clear-all");
    gtk_widget_set_size_request(btn_clear, -1, 56);
    gtk_widget_set_margin_top(btn_clear, 12);
    g_signal_connect(btn_clear, "clicked", G_CALLBACK(on_clear_all_clicked), NULL);
    gtk_box_append(GTK_BOX(current_switcher_box), btn_clear);
}

GtkWidget *wm_create_task_switcher_view(void (*on_return_home)(void))
{
    global_on_return_home = on_return_home;

    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(container, 28);
    gtk_widget_set_margin_end(container, 28);
    gtk_widget_set_margin_top(container, 20);
    gtk_widget_set_margin_bottom(container, 16);

    current_switcher_box = container;
    refresh_switcher_ui();

    return container;
}
