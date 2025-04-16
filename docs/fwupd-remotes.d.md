---
title: fwupd remote file format
---

% fwupd-remotes.d(5) {{PACKAGE_VERSION}} | Remote File Format

## NAME

**fwupd-remotes.d** — remotes used for the fwupd daemon.

## SYNOPSIS

The `{{SYSCONFDIR}}/fwupd/remotes.d` and `{{LOCALSTATEDIR}}/fwupd/remotes.d` directories are
used to read information about remote metadata sources.
The complete description of the file format and possible parameters are documented here for
reference purposes.

## FILE FORMAT

The file consists of a multiple sections with optional parameters. Parameters are of the form:

```text
[section]
key = value
```

The file is line-based, each newline-terminated line represents either a comment, a section name or
a parameter.

Section and parameter names are case sensitive.

Only the first equals sign in a parameter is significant.
Whitespace before or after the first equals sign is discarded as is leading and trailing whitespace
in a parameter value.
Internal whitespace within a parameter value is retained.

Any line beginning with a hash (`#`) character is ignored, as are lines containing only whitespace.

The values following the equals sign in parameters are all either a string (no quotes needed),
unsigned integers, or a boolean, which may be given as **true** or **false**.
Case is not significant in boolean values, but is preserved in string values.

## REMOTE PARAMETERS

The `[fwupd Remote]` section can contain the following parameters:

**Enabled=false**

  If the remote should be considered when finding releases for devices.
  Only enabled remotes are refreshed when using `fwupdmgr refresh` and when considering what updates
  are available for each device. This value can be modified using `fwupdmgr enable-remote`.

**Title=**

  The single line description to show in any UI tools.

**MetadataURI=**

  The URL of AppStream metadata to download and use. This should have a suffix of `.xml.gz` for
  legacy metadata and `.xml.xz` for the more modern format.
  Only prefixes of `http://`, `https://` and `file://` are supported here.

**FirmwareBaseURI=**

  The optional base URL of the cabinet archives to download.
  If not specified the `MetadataURI` base URL is used.
  Only prefixes of `http://`, `https://` and `file://` are supported here.

**ApprovalRequired=false**

  If set to `true` then only releases allow-listed with `fwupdmgr set-approved-firmware` will show
  in CLI and GUI tools.

**ReportURI=**

  The endpoint to use for sending success reports for firmware obtained from this remote,
  or blank to disable this feature.

**AutomaticReports=false**

  If `true`, automatically sent success reports for firmware obtained from this remote after the
  firmware update has completed.

**AutomaticSecurityReports=false**

  If `true`, automatically sent HSI platform security reports when running `fwupdmgr security`.

**OrderBefore=**

  This remote will be ordered before any remotes listed here, using commas as the delimiter.

  **NOTE:** When the same firmware release is available from multiple remotes, the one with the
  highest priority will be used.

**OrderAfter=**

  This remote will be ordered after any remotes listed here, using commas as the delimiter.

**Username=**

  The username to use for BASIC authentication when downloading metadata and firmware from this
  remote, and for uploading success reports.

**Password=**

  The password (although, in practice this will be a user *token*) to use for BASIC authentication
  when downloading both metadata and firmware from this remote, and for uploading success reports.

**RefreshInterval={{FWUPD_REMOTE_CONFIG_DEFAULT_REFRESH_INTERVAL}}**

  The time in seconds after which the front end tools should re-download the metadata signature,
  or `0` to re-download every time.

## NOTES

The basename of the path without the extension is used for the remote ID.
For instance, the `{{SYSCONFDIR}}/fwupd/remotes.d/lvfs.conf` remote file will have ID of `lvfs`.

## SEE ALSO

<fwupd.conf(5)>
