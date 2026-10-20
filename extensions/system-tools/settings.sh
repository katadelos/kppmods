#!/bin/sh
set -eu
# A string in the bundle can outlive the Settings module that used it.
runtime_config=$(/var/run/eink-sdk/usr/bin/kppmod configuration)
if printf '%s\n' "$runtime_config" | grep -Fq '"settingsRoot":'; then
    # Use the Settings layer so Close can dismiss the view.
    exec /usr/bin/lipc-set-prop com.lab126.appmgrd startView \
        'KPP_SETTINGS:5:app://com.lab126.KPPMainApp?view=KPP_SETTINGS&activeViewName=settings'
fi
exec /usr/bin/lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.booklet.settings'
