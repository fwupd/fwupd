# Test that the fwupd-refresh service runs successfully with the new polkit configuration
import subprocess

def test_fwupd_refresh():
    # Run the fwupd-refresh service
    try:
        output = subprocess.check_output(['fwupd-refresh'])
        print(output.decode('utf-8'))
    except subprocess.CalledProcessError as e:
        print(f"Error running fwupd-refresh: {e}")
        assert False

if __name__ == "__main__":
    test_fwupd_refresh()