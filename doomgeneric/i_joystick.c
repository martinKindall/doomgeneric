#include "doomtype.h"
#include "i_joystick.h"

// We disable all joystick logic for the UEFI build
void I_InitJoystick(void)
{
}

void I_UpdateJoystick(void)
{
}

void I_ShutdownJoystick(void)
{
}

void I_BindJoystickVariables(void)
{
    // Do not bind any variables to m_config.c 
    // to prevent joystick settings from appearing in config files.
}
