/* Aseprite
 * Copyright (C) 2001-2013  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/ui/skin/button_icon_impl.h"

#include "app/ui/skin/skin_theme.h"
#include "she/surface.h"

namespace app {

using namespace app::skin;

ButtonIconImpl::ButtonIconImpl(SkinTheme* theme,
                               int normalIcon,
                               int selectedIcon,
                               int disabledIcon,
                               int iconAlign)
  : m_theme(theme)
  , m_normalIcon(normalIcon)
  , m_selectedIcon(selectedIcon)
  , m_disabledIcon(disabledIcon)
  , m_iconAlign(iconAlign)
{
  ASSERT(theme != NULL);
}

void ButtonIconImpl::destroy()
{
  delete this;
}

int ButtonIconImpl::getWidth()
{
  return m_theme->get_part(m_normalIcon)->width();
}

int ButtonIconImpl::getHeight()
{
  return m_theme->get_part(m_normalIcon)->height();
}

she::Surface* ButtonIconImpl::getNormalIcon()
{
  return m_theme->get_part(m_normalIcon);
}

she::Surface* ButtonIconImpl::getSelectedIcon()
{
  return m_theme->get_part(m_selectedIcon);
}

she::Surface* ButtonIconImpl::getDisabledIcon()
{
  return m_theme->get_part(m_disabledIcon);
}

int ButtonIconImpl::getIconAlign()
{
  return m_iconAlign;
}

} // namespace app
