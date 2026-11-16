#!/usr/bin/env bash
# Provision AWS IoT credentials into the SPIFFS image, then flash it.
set -euo pipefail
OTA_REPO="${STARFORGE_OTA_REPO:-$HOME/git/starforge-ota}"
THING="${1:?usage: provision-aws-fs.sh <thing-name>}"
ENV="${PIOENV:-esp32-s3-touch}"
SRC="$OTA_REPO/infra/secrets/$THING"

[ -f "$SRC/cert.pem" ]   || { echo "missing $SRC/cert.pem (run tofu apply in starforge-ota)" >&2; exit 1; }
[ -f "$SRC/device.key" ] || { echo "missing $SRC/device.key" >&2; exit 1; }

EP=$(aws iot describe-endpoint --endpoint-type iot:Data-ATS --query endpointAddress --output text)

mkdir -p data/aws
cp "$SRC/cert.pem"        data/aws/cert.pem
cp "$SRC/device.key"      data/aws/device.key
chmod 600                 data/aws/device.key
printf '%s\n' "$EP"    >  data/aws/endpoint.txt
printf '%s\n' "$THING" >  data/aws/thing.txt

# Amazon Root CA 1, pinned by URL; verify the SHA-256 if you cache it.
curl -fsSL https://www.amazontrust.com/repository/AmazonRootCA1.pem -o data/aws/root-CA.crt

pio run -e "$ENV" -t uploadfs
