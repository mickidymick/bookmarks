#!/bin/bash
gcc -o bookmarks.so bookmarks.c $(yed --print-cflags) $(yed --print-ldflags)
