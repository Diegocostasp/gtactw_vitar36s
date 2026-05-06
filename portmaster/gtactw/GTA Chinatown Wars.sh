#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"

export PORT_32BIT="Y"

[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR=/$directory/ports/gtactw/gtactw

cd "$GAMEDIR" || exit 1

mkdir -p "$GAMEDIR/data"
mkdir -p "$GAMEDIR/conf"

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

echo "Starting GTA CTW PortMaster"
echo "GAMEDIR=$GAMEDIR"
echo "DEVICE_ARCH=$DEVICE_ARCH"
echo "CFW_NAME=$CFW_NAME"

export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export GTACTW_DATA_PATH="$GAMEDIR/data"

if [ ! -f "$GAMEDIR/data/libCTW.so" ]; then
  echo "ERROR: libCTW.so not found."
  echo "Put it here:"
  echo "$GAMEDIR/data/libCTW.so"
  sleep 5
  exit 1
fi

chmod +x "$GAMEDIR/gtactw.armhf"

pm_platform_helper "$GAMEDIR/gtactw.armhf"

./gtactw.armhf

pm_finish
