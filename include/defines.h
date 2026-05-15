//
// Created by jamal on 15/05/2026.
//

#ifndef DEFINES_H
#define DEFINES_H

#include <string>

#define COMMAND_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY()  \
cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT()  \
cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif


#endif //DEFINES_H
