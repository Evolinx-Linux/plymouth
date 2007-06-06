/* ply-boot-splash.h - APIs for putting up a splash screen
 *
 * Copyright (C) 2007 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by: Ray Strode <rstrode@redhat.com>
 */
#include "config.h"
#include "ply-boot-splash.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "ply-boot-splash-plugin.h"
#include "ply-event-loop.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-utils.h"

struct _ply_boot_splash
{
  ply_event_loop_t *loop;
  ply_module_handle_t *module_handle;
  const ply_boot_splash_plugin_interface_t *plugin_interface;
  ply_boot_splash_plugin_t *plugin;

  char *module_name;
  char *status;

  uint32_t is_shown : 1;
};

typedef const ply_boot_splash_plugin_interface_t *
        (* get_plugin_interface_function_t) (void);

ply_boot_splash_t *
ply_boot_splash_new (const char *module_name)
{
  ply_boot_splash_t *splash;

  assert (module_name != NULL);

  splash = calloc (1, sizeof (ply_boot_splash_t));
  splash->loop = NULL;
  splash->module_name = strdup (module_name);
  splash->module_handle = NULL;
  splash->is_shown = false;

  return splash;
}

void
ply_boot_splash_free (ply_boot_splash_t *splash)
{
  if (splash == NULL)
    return;

  if (splash->is_shown)
    ply_boot_splash_hide (splash);

  free (splash->module_name);
  free (splash);
}

static bool
ply_boot_splash_load_plugin (ply_boot_splash_t *splash)
{
  assert (splash != NULL);
  assert (splash->module_name != NULL);

  get_plugin_interface_function_t get_boot_splash_plugin_interface;

  splash->module_handle = ply_open_module (splash->module_name);

  if (splash->module_handle == NULL)
    return false;

  get_boot_splash_plugin_interface = (get_plugin_interface_function_t)
      ply_module_look_up_function (splash->module_handle,
                                   "ply_boot_splash_plugin_get_interface");

  if (get_boot_splash_plugin_interface == NULL)
    {
      ply_close_module (splash->module_handle);
      splash->module_handle = NULL;
      return false;
    }

  splash->plugin_interface = get_boot_splash_plugin_interface ();

  if (splash->plugin_interface == NULL)
    {
      ply_close_module (splash->module_handle);
      splash->module_handle = NULL;
      return false;
    }

  splash->plugin = splash->plugin_interface->create_plugin ();

  assert (splash->plugin != NULL);

  return true;
}

static void
ply_boot_splash_unload_plugin (ply_boot_splash_t *splash)
{
  assert (splash != NULL);
  assert (splash->plugin != NULL);
  assert (splash->plugin_interface != NULL);
  assert (splash->module_handle != NULL);

  splash->plugin_interface->destroy_plugin (splash->plugin);
  splash->plugin = NULL;

  ply_close_module (splash->module_handle);
  splash->plugin_interface = NULL;
  splash->module_handle = NULL;
}

bool
ply_boot_splash_show (ply_boot_splash_t *splash)
{
  assert (splash != NULL);
  assert (splash->module_name != NULL);
  assert (splash->loop != NULL);

  if (!ply_boot_splash_load_plugin (splash))
    return false;

  assert (splash->plugin_interface != NULL);
  assert (splash->plugin != NULL);
  assert (splash->plugin_interface->attach_to_event_loop != NULL);
  assert (splash->plugin_interface->show_splash_screen != NULL);

  splash->plugin_interface->attach_to_event_loop (splash->plugin,
                                                  splash->loop);
  if (!splash->plugin_interface->show_splash_screen (splash->plugin))
    return false;

  splash->is_shown = true;
  return true;
}

void
ply_boot_splash_update_status (ply_boot_splash_t *splash,
                               const char        *status)
{
  assert (splash != NULL);
  assert (status != NULL);
  assert (splash->plugin_interface != NULL);
  assert (splash->plugin != NULL);
  assert (splash->plugin_interface->update_status != NULL);
  assert (splash->is_shown);

  splash->plugin_interface->update_status (splash->plugin, status);
}

void
ply_boot_splash_hide (ply_boot_splash_t *splash)
{
  assert (splash != NULL);
  assert (splash->plugin_interface != NULL);
  assert (splash->plugin != NULL);
  assert (splash->plugin_interface->hide_splash_screen != NULL);

  splash->plugin_interface->hide_splash_screen (splash->plugin);

  ply_boot_splash_unload_plugin (splash);
  splash->is_shown = false;
}

static void
ply_boot_splash_detach_from_event_loop (ply_boot_splash_t *splash)
{
  assert (splash != NULL);
  splash->loop = NULL;
}

void
ply_boot_splash_attach_to_event_loop (ply_boot_splash_t *splash,
                                      ply_event_loop_t  *loop)
{
  assert (splash != NULL);
  assert (loop != NULL);
  assert (splash->loop == NULL);

  splash->loop = loop;

  ply_event_loop_watch_for_exit (loop, (ply_event_loop_exit_handler_t) 
                                 ply_boot_splash_detach_from_event_loop,
                                 splash); 
}

#ifdef PLY_BOOT_SPLASH_ENABLE_TEST

#include <stdio.h>

#include "ply-event-loop.h"
#include "ply-boot-splash.h"

int
main (int    argc,
      char **argv)
{
  ply_event_loop_t *loop;
  ply_boot_splash_t *splash;
  int exit_code;

  exit_code = 0;

  loop = ply_event_loop_new ();

  splash = ply_boot_splash_new ("../splash-plugins/.libs/fedora-fade-in.so");
  ply_boot_splash_attach_to_event_loop (splash, loop);

  if (!ply_boot_splash_show (splash))
    {
      perror ("could not show splash screen");
      return errno;
    }

  exit_code = ply_event_loop_run (loop);
  ply_boot_splash_free (splash);

  return exit_code;
}

#endif /* PLY_BOOT_SPLASH_ENABLE_TEST */
/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
