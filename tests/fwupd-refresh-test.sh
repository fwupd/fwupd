# Test that the fwupd-refresh service runs successfully with the new polkit configuration
#!/bin/bash

# Run the fwupd-refresh service
output=$(fwupd-refresh)
if [ $? -eq 0 ]; then
    echo "fwupd-refresh ran successfully"
else
    echo "Error running fwupd-refresh: $output"
    exit 1
fi