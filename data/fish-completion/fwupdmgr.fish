function __fish_fwupdmgr_devices --description 'Get device IDs used by fwupdmgr'
    set -l ids (fwupdmgr get-devices 2>/dev/null | string replace -f -r '.*Device ID:\s*(.*)' '$1')
    set -l names (fwupdmgr get-devices 2>/dev/null | string replace -f -r '.*─(.*):$' '$1')
    for i in (seq (count $ids))
        echo -e "$ids[$i]\t$names[$i]"
    end
end

function __fish_fwupdmgr_remotes --description 'Get remote IDs used by fwupdmgr'
    fwupdmgr get-remotes 2>/dev/null | string replace -f -r '.*Remote ID:\s*(.*)' '$1'
end

function __fish_fwupdmgr_bios_settings --description 'Get BIOS setting names via fwupdmgr'
    fwupdmgr get-bios-settings --no-authenticate 2>/dev/null | string replace -f -r '^(\S+):$' '$1'
end

function __fish_fwupdmgr_subcommands --description 'Get fwupdmgr subcommands'
    printf '%s\t%s\n' \
        activate 'Activate devices' \
        block-firmware 'Blocks a specific firmware from being installed' \
        check-reboot-needed 'Check if any devices are pending a reboot to complete update' \
        clear-results 'Clears the results from the last update' \
        device-emulate 'Emulate a device using a JSON manifest' \
        device-test 'Test a device using a JSON manifest' \
        device-wait 'Wait for a device to appear' \
        disable-remote 'Disables a given remote' \
        downgrade 'Downgrades the firmware on a device' \
        download 'Download a file' \
        emulation-load 'Load device emulation data' \
        emulation-save 'Save device emulation data' \
        emulation-tag 'Adds devices to watch for future emulation' \
        emulation-untag 'Removes devices to watch for future emulation' \
        enable-remote 'Enables a given remote' \
        get-approved-firmware 'Gets the list of approved firmware' \
        get-bios-settings 'Retrieve BIOS settings' \
        get-blocked-firmware 'Gets the list of blocked firmware' \
        get-details 'Gets details about a firmware file' \
        get-devices 'Get all devices that support firmware updates' \
        get-history 'Show history of firmware updates' \
        get-plugins 'Get all enabled plugins registered with the system' \
        get-releases 'Gets the releases for a device' \
        get-remotes 'Gets the configured remotes' \
        get-results 'Gets the results from the last update' \
        get-updates 'Gets the list of updates for connected hardware' \
        inhibit 'Inhibit the system to prevent upgrades' \
        install 'Install a specific firmware file on all devices that match' \
        local-install 'Install a firmware file in cabinet format on this hardware' \
        modify-config 'Modifies a daemon configuration value' \
        modify-remote 'Modifies a given remote' \
        quit 'Asks the daemon to quit' \
        refresh 'Refresh metadata from remote server' \
        reinstall 'Reinstall current firmware on the device' \
        report-devices 'Upload the list of updatable devices to a remote server' \
        report-export 'Export firmware history for manual upload' \
        report-history 'Share firmware history with the developers' \
        reset-config 'Resets a daemon configuration section' \
        search 'Finds firmware releases from the metadata' \
        security 'Gets the host security attributes' \
        security-fix 'Fix a specific host security attribute' \
        security-undo 'Undo the host security attribute fix' \
        set-approved-firmware 'Sets the list of approved firmware' \
        set-bios-setting 'Sets one or more BIOS settings' \
        switch-branch 'Switch the firmware branch on the device' \
        sync 'Sync firmware versions to the chosen configuration' \
        unblock-firmware 'Unblocks a specific firmware from being installed' \
        uninhibit 'Uninhibit the system to allow upgrades' \
        unlock 'Unlocks the device for firmware access' \
        update 'Updates all firmware to latest versions available' \
        verify 'Checks cryptographic hash matches firmware' \
        verify-update 'Update the stored cryptographic hash with current ROM contents'
end


# complete options
complete -c fwupdmgr -s h -l help -d 'Show help options'
complete -c fwupdmgr -s v -l verbose -d 'Show extra debugging information'
complete -c fwupdmgr -l version -d 'Show client and daemon versions'
complete -c fwupdmgr -l download-retries -x -d 'Set the download retries for transient errors'
complete -c fwupdmgr -l allow-reinstall -d 'Allow reinstalling existing firmware versions'
complete -c fwupdmgr -l allow-older -d 'Allow downgrading firmware versions'
complete -c fwupdmgr -l allow-branch-switch -d 'Allow switching firmware branch'
complete -c fwupdmgr -l only-emulated -d 'Only install onto emulated devices'
complete -c fwupdmgr -l force -d 'Force the action by relaxing some runtime checks'
complete -c fwupdmgr -s y -l assume-yes -d 'Answer yes to all questions'
complete -c fwupdmgr -l sign -d 'Sign the uploaded data with the client certificate'
complete -c fwupdmgr -l no-unreported-check -d 'Do not check for unreported history'
complete -c fwupdmgr -l no-metadata-check -d 'Do not check for old metadata'
complete -c fwupdmgr -l no-remote-check -d 'Do not check if download remotes should be enabled'
complete -c fwupdmgr -l no-reboot-check -d 'Do not check or prompt for reboot after update'
complete -c fwupdmgr -l no-safety-check -d 'Do not perform device safety checks'
complete -c fwupdmgr -l no-device-prompt -d 'Do not prompt for devices'
complete -c fwupdmgr -l no-history -d 'Do not write to the history database'
complete -c fwupdmgr -l show-all -d 'Show all results'
complete -c fwupdmgr -l disable-ssl-strict -d 'Ignore SSL strict checks when downloading files'
complete -c fwupdmgr -l p2p -d 'Only use peer-to-peer networking when downloading files'
complete -c fwupdmgr -l filter -d 'Filter with a set of device flags'
complete -c fwupdmgr -l filter-release -d 'Filter with a set of release flags'
complete -c fwupdmgr -l json -d 'Output in JSON format'
complete -c fwupdmgr -l no-security-fix -d 'Do not prompt to fix security issues'
complete -c fwupdmgr -l no-authenticate -d 'Don\'t prompt for authentication'

# complete subcommands
complete -c fwupdmgr -n '__fish_use_subcommand' -x -a "(__fish_fwupdmgr_subcommands)"

# commands exclusively consuming device IDs
set -l deviceid_consumers activate check-reboot-needed clear-results device-wait downgrade emulation-tag emulation-untag get-releases get-results get-updates install reinstall switch-branch unlock update verify verify-update
# complete device IDs
complete -c fwupdmgr -n "__fish_seen_subcommand_from $deviceid_consumers" -x -a "(__fish_fwupdmgr_devices)"
# complete files and device IDs
complete -c fwupdmgr -n "__fish_seen_subcommand_from local-install" -r -a "(__fish_fwupdmgr_devices)"

# commands exclusively consuming remote IDs
set -l remoteid_consumers disable-remote enable-remote modify-remote
# complete remote IDs
complete -c fwupdmgr -n "__fish_seen_subcommand_from $remoteid_consumers" -x -a "(__fish_fwupdmgr_remotes)"
# complete files and remote IDs
complete -c fwupdmgr -n "__fish_seen_subcommand_from refresh" -r -a "(__fish_fwupdmgr_remotes)"

# complete BIOS settings
complete -c fwupdmgr -n "__fish_seen_subcommand_from get-bios-settings set-bios-setting" -x -a "(__fish_fwupdmgr_bios_settings)"
