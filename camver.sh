#!/bin/bash
# https://gist.github.com/Hermann-SW/3c0387c4340e10866af32cc5a3d21036

dt=`vcgencmd get_camera | grep "detected=1"`
if [ "$dt" = "" ]; then
  echo "no camera detected"
else
  if [ "`which i2cdetect`" = "" ]; then
    echo "i2cdetect not installed" ; exit
  fi
  cd `dirname $0`
  if [[ ! -a camera_i2c ]]; then
    wget https://raw.githubusercontent.com/6by9/raspiraw/master/camera_i2c \
      2>/dev/null
  fi
  r=`uname -r | head --bytes 1`
  if [ "$r" = "4" ]; then i2c=0; else i2c=10; fi
  bash camera_i2c 2>&1 | cat > /dev/null
  v1=`i2cdetect -y $i2c 54 54 | grep " 36"`
  v2=`i2cdetect -y $i2c 16 16 | grep " 10"`
  hq=`i2cdetect -y $i2c 26 26 | grep " 1a"`
  if [ "$v1" != "" ]; then echo -n "v1"; fi
  if [ "$v2" != "" ]; then echo -n "v2"; fi
  if [ "$hq" != "" ]; then echo -n "hq"; fi
  echo " camera found"
fi
