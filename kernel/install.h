#ifndef INSTALL_H
#define INSTALL_H

#include <stdint.h>

void install_set_installer_mode(int on);
int  install_get_installer_mode(void);
int  install_to_drive(int drive);

#endif