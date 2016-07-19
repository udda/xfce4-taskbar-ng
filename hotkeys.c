#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <xfconf/xfconf.h>
#include <libxfce4util/libxfce4util.h>
#include <gtkhotkey.h>
#include "taskbar-widget.h"

#define TASKBAR_XFCONF_CHANNEL "xfce4-keyboard-shortcuts"
#define TASKBAR_XFCONF_PREFIX "/xftaskbar"

typedef struct _hotkey_cb_data {
	char *cmd;
	XfceTaskBar *taskbar;
} hotkey_cb_data;

typedef struct _HotkeysHandler {
	XfconfChannel *xfconf_channel;
	char *xfconf_prefix;
	XfceTaskBar *taskbar;
	GHashTable *keymap;
	GtkHotkeyListener *listener;
	/* To be freed */
	GArray *hotkey_cbs;
} HotkeysHandler;

gchar *strip_path(gchar *str) {
	gchar **chunks = g_strsplit_set(str, "/", 0);
	gchar *ret = NULL;
	while(chunks[0]) {
		ret = chunks[0];
		chunks++;
	}
	return(ret);
}

void create_default_keymap(XfconfChannel *channel) {
	int i;
	char key[32];
	char value[16];
	for(i = 1; i <= 10; i++) {
		g_sprintf(key, "%s/<Super>%d", TASKBAR_XFCONF_PREFIX, i % 10);
		g_sprintf(value, "selgrp %d", i);
		xfconf_channel_set_string(channel, key, value);
	}
	return;
}

void hotkey_activated_callback (GtkHotkeyInfo *hotkey, guint event_time, hotkey_cb_data *data) {
	int arg;
	if(sscanf(data->cmd, "selgrp %d", &arg)) {
			xfce_taskbar_selgrp_cmd(data->taskbar, arg);
	} else {
		g_message("xfce4-taskbar-plugin: unknown command %s", data->cmd);
	}
}

void hotkey_parsed_callback (gpointer key, gpointer unused_value, HotkeysHandler *handler) {
	char *value = xfconf_channel_get_string(handler->xfconf_channel, key, NULL);
	g_assert(value != NULL);
	key = strip_path(key);
	g_assert(key != NULL);
	GtkHotkeyInfo *hotkey = gtk_hotkey_info_new("xftaskbar",value,key,NULL);
	GError *error = NULL;
	gtk_hotkey_info_bind (hotkey, &error);
	if (error) {
		g_critical ("Error binding hotkey: for '%s': %s",
				    (char*) key, error->message);
	}
	hotkey_cb_data *data = g_malloc(sizeof(hotkey_cb_data));
	g_array_append_val(handler->hotkey_cbs, data);
	data->cmd = value;
	data->taskbar = handler->taskbar;
	g_signal_connect (hotkey, "activated",
                      G_CALLBACK(hotkey_activated_callback), data);
}

HotkeysHandler *init_global_hotkeys(XfceTaskBar *taskbar) {
	HotkeysHandler *handler = g_malloc(sizeof(HotkeysHandler));
	handler->xfconf_channel = xfconf_channel_get(TASKBAR_XFCONF_CHANNEL);
	if(!handler->xfconf_channel)
	    	g_critical("Couldn't acquire channel: %s", TASKBAR_XFCONF_CHANNEL);
	handler->xfconf_prefix = TASKBAR_XFCONF_PREFIX;
	handler->taskbar = taskbar;
	handler->keymap = xfconf_channel_get_properties(handler->xfconf_channel, handler->xfconf_prefix);
	handler->hotkey_cbs = g_array_new(FALSE, FALSE, sizeof(hotkey_cb_data));
	if(!g_hash_table_size(handler->keymap)) {
		/* The config doesn't exists yet (e.g. first run).
		   Initializing with defaults. */
		g_message("xfce4-taskbar-plugin: creating default keymap");
		create_default_keymap(handler->xfconf_channel);
		handler->keymap = xfconf_channel_get_properties(handler->xfconf_channel, handler->xfconf_prefix);
	}
	handler->listener = gtk_hotkey_listener_get_default();
	g_hash_table_foreach(handler->keymap, (GHFunc)hotkey_parsed_callback, handler);
	return(handler);
}

void finish_global_hotkeys(HotkeysHandler *handler) {
	// TODO: hotkey_cbs
	g_array_free(handler->hotkey_cbs, TRUE);
	g_free(handler);
}
