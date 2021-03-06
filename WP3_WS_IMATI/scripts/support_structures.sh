#!/bin/bash

TOKEN_GSS=$1
INPUT_MODEL_GSS=$2
OUTPUT_MODEL_GSS=$3

DENSITY=$4
THRESHOLD=$5

TARGET_DIR=$6
STATUS_FILE=$7
RESULT_FILE=$8

OUTPUT_FILE=$9

DOWNLOAD_TOOL="/root/infrastructureClients/gssClients/gssPythonClients/download_gss.py"
UPLOAD_TOOL="/root/infrastructureClients/gssClients/gssPythonClients/upload_gss.py"

SUPPORT_STRUCTURES_TOOL="/root/CaxMan/demo_services/support_structures/build/support_structures"

echo "20" > $TARGET_DIR/$STATUS_FILE

DOWNLOADED_FILE=$TARGET_DIR/input.off

rm -f $DOWNLOADED_FILE
rm -f $OUTPUT_FILE

# download file from gss
cmd_download="python $DOWNLOAD_TOOL $INPUT_MODEL_GSS $DOWNLOADED_FILE $TOKEN_GSS"

echo $cmd_download

$cmd_download


echo "40" > $TARGET_DIR/$STATUS_FILE

# run orientation optimization
$SUPPORT_STRUCTURES_TOOL $DOWNLOADED_FILE $OUTPUT_FILE $DENSITY $THRESHOLD

echo "70" > $TARGET_DIR/$STATUS_FILE

# upload outputt file to gss
cmd_upload="python $UPLOAD_TOOL $OUTPUT_MODEL_GSS $OUTPUT_FILE $TOKEN_GSS"

echo $cmd_upload

echo "90" > $TARGET_DIR/$STATUS_FILE

$cmd_upload > $RESULT_FILE
