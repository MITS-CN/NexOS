#ifndef INSTALL_H
#define INSTALL_H

#include <stdint.h>

extern const uint8_t *g_init_elf_data;
extern uint32_t       g_init_elf_size;
extern const uint8_t *g_kbd_elf_data;
extern uint32_t       g_kbd_elf_size;
extern const uint8_t *g_vga_elf_data;
extern uint32_t       g_vga_elf_size;
/* ★ S5.5 */
extern const uint8_t *g_mouse_elf_data;
extern uint32_t       g_mouse_elf_size;

void install_set_installer_mode(int on);
int  install_get_installer_mode(void);
int  install_to_drive(int drive);

#endif