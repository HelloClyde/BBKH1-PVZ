#!/bin/sh
verbose=1

convert_if_candidate()
{
    if [ $verbose -eq 1 ]; then
        echo "checking $1 ..."
    fi

    [ -d "$1" ] && return;
    [ ! -f "$1" ] && return;

    echo "$1" | egrep -q ".*\.jpg$"

# change gamma for jpg files
    if [ $? -eq 0 ]; then
        convert "$1"
        continue;
    fi

    echo "$1" | egrep -q ".*\.png$"

    if [ $? -eq 0 ]; then
        convert "$1"
        continue;
    fi

    echo "$1" | egrep -q ".*\.gif$"

    if [ $? -eq 0 ]; then
        convert "$1"
        continue;
    fi

    if [ $verbose -eq 1 ]; then
        echo "skipping $1"
    fi
}

convert()
{
#   chmod u+w $1

    [ ! -f "$1" ] && return

    if [ $verbose -eq 1 ]; then
        echo "converting gamma of $1"
    fi
    /opt/local/bin/convert -gamma .8 "$1" "$1"
    [ $? -ne 0 ] && echo "error converting $1"
}

# will fail if file or path has a space in it. Probably need to do the while read thing...
for f in `find .`; do
    convert_if_candidate $f
done

# this will handle those paths with a space in them but skip everything else
find . -name '* *' | while read f; do # only reads those containing a space
    convert_if_candidate "$f"
done
