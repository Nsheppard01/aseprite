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

#include "app/commands/cmd_export_sprite_sheet.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/commands/params.h"
#include "app/context.h"
#include "app/context_access.h"

namespace app {

class RepeatLastExportCommand : public Command {
public:
  RepeatLastExportCommand();
  Command* clone() const override { return new RepeatLastExportCommand(*this); }

protected:
  virtual bool onEnabled(Context* context) override;
  virtual void onExecute(Context* context) override;
};

RepeatLastExportCommand::RepeatLastExportCommand()
  : Command("RepeatLastExport",
            "Repeat Last Export",
            CmdRecordableFlag)
{
}

bool RepeatLastExportCommand::onEnabled(Context* context)
{
  return context->checkFlags(ContextFlags::ActiveDocumentIsWritable);
}

void RepeatLastExportCommand::onExecute(Context* context)
{
  base::UniquePtr<ExportSpriteSheetCommand> command(
    static_cast<ExportSpriteSheetCommand*>(
      CommandsModule::instance()->getCommandByName(CommandId::ExportSpriteSheet)->clone()));

  {
    const ContextReader reader(context);
    const Document* document(reader.document());
    doc::ExportDataPtr data = document->exportData();

    if (data != NULL) {
      if (data->type() == doc::ExportData::None)
        return;                 // Do nothing case

      command->setUseUI(false);
      command->setExportData(data);
    }
  }

  context->executeCommand(command);
}

Command* CommandFactory::createRepeatLastExportCommand()
{
  return new RepeatLastExportCommand;
}

} // namespace app
