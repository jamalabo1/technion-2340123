//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef IMPL_H
#define IMPL_H

#include "command.h"
#include "built_in.h"
#include "external.h"
#include "special.h"




class USBInfoCommand : public Command {
    // TODO: Add your data members **BONUS: 10 Points**
public:
    USBInfoCommand(const char* cmd_line);

    virtual ~USBInfoCommand() {
    }

    void execute() override;
};



#endif //IMPL_H
