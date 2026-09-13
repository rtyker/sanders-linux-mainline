/*
 * app_drawer.h — Contrato de Interface para a Fase 2 (Gaveta de Apps & Teclado)
 * Módulo de responsabilidade do Agente 1 (ex: BF)
 */
#ifndef APP_DRAWER_H
#define APP_DRAWER_H

#include <gtk/gtk.h>

typedef void (*AppLaunchCallback)(const char *exec_cmd);

GtkWidget *app_drawer_create(AppLaunchCallback on_launch);
void       app_drawer_refresh_list(void);
GtkWidget *keyboard_create_popup(GtkWidget *target_entry);

#endif /* APP_DRAWER_H */
