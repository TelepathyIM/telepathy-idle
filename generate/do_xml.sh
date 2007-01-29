#!/bin/sh

if [ `basename $PWD` == "generate" ]; then
  TP=${TELEPATHY_PYTHON:=$PWD/../../telepathy-python}
else
  TP=${TELEPATHY_PYTHON:=$PWD/../telepathy-python}
fi

export PYTHONPATH=$TP:$PYTHONPATH

test -d generate && cd generate
cd xml-pristine

echo "Generating pristine XML in generate/xml-pristine..."
python2.4 $TP/tools/genxml.py ../idle.def
