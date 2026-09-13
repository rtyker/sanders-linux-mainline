/*
 * window_manager.h — Gerenciador de Janelas, Multitarefas e Navegação Android 3-Button
 * Módulo da Fase 3 — Agente Antigravity
 */
#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <gtk/gtk.h>
#include <sys/types.h>
#include <time.h>

typedef struct {
    pid_t pid;
    char name[64];
    char icon[16];
    char cmd[256];
    time_t start_time;
    gboolean is_active;
} AppTask;

/* Inicialização do subsistema de janelas */
void       wm_init(void);

/* Registro de nova aplicação disparada pelo launcher */
void       wm_register_launched_app(pid_t pid, const char *name, const char *icon, const char *cmd);

/* Encerramento de tarefa individual ou de todas */
void       wm_kill_task(pid_t pid);
void       wm_clear_all_tasks(void);

/* Ações dos botões da barra de navegação móvel */
void       wm_action_home(GtkWindow *launcher_win);
void       wm_action_back(GtkWindow *launcher_win);

/* Retorna a lista atual de tarefas */
GList     *wm_get_task_list(void);
gboolean   wm_has_active_tasks(void);

/* Widget visual do Alternador de Tarefas / Recentes estilo Android */
GtkWidget *wm_create_task_switcher_view(void (*on_return_home)(void));

#endif /* WINDOW_MANAGER_H */
