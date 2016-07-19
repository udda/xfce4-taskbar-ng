/*
 * Copyright (C) 2008-2010 Nick Schermer <nick@xfce.org>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef __XFCE_TASKBAR_H__
#define __XFCE_TASKBAR_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _XfceTaskBarClass     XfceTaskBarClass;
typedef struct _XfceTaskBar          XfceTaskBar;
typedef enum   _XfceTaskBarGrouping  XfceTaskBarGrouping;

#define XFCE_TYPE_taskbar            (xfce_taskbar_get_type ())
#define XFCE_taskbar(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XFCE_TYPE_taskbar, XfceTaskBar))
#define XFCE_TASKBAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), XFCE_TYPE_taskbar, XfceTaskBarClass))
#define XFCE_IS_taskbar(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XFCE_TYPE_taskbar))
#define XFCE_IS_TASKBAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), XFCE_TYPE_taskbar))
#define XFCE_TASKBAR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), XFCE_TYPE_taskbar, XfceTaskBarClass))


GType xfce_taskbar_get_type                (void) G_GNUC_CONST;

void  xfce_taskbar_set_orientation         (XfceTaskBar   *taskbar,
                                             GtkOrientation  orientation);

void  xfce_taskbar_set_size                (XfceTaskBar   *taskbar,
                                             gint            size);

void  xfce_taskbar_update_monitor_geometry (XfceTaskBar   *taskbar);

G_END_DECLS

#endif /* !__XFCE_TASKBAR_H__ */
