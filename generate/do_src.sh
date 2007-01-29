#!/bin/sh

if [ `basename $PWD` == "generate" ]; then
  TP=${TELEPATHY_PYTHON:=$PWD/../../telepathy-python}
else
  TP=${TELEPATHY_PYTHON:=$PWD/../telepathy-python}
fi

export PYTHONPATH=$TP:$PYTHONPATH

test -d generate && cd generate
cd src

echo Generating IdleConnectionManager files ...
python2.4 $TP/tools/gengobject.py ../xml-modified/idle-connection-manager.xml IdleConnectionManager

echo Generating IdleConnection files ...
python2.4 $TP/tools/gengobject.py ../xml-modified/idle-connection.xml IdleConnection

echo Generating IdleIMChannel files ...
python2.4 $TP/tools/gengobject.py ../xml-modified/idle-im-channel.xml IdleIMChannel

echo Generating IdleMUCChannel files ...
python2.4 $TP/tools/gengobject.py ../xml-modified/idle-muc-channel.xml IdleMUCChannel

echo Generating error enums ...
python2.4 $TP/tools/generrors.py
