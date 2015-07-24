<!DOCTYPE html>
<!-- Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
     Licensed under the GNU General Public License Version 2 -->
<html>
<head>
<title>Linux Vendor Firmware Service</title>
<meta http-equiv="Content-Type" content="text/html;charset=utf-8"/>
<link rel="stylesheet" href="style.css" type="text/css" media="screen"/>
<link rel="shortcut icon" href="favicon.ico"/>
</head>

<body>

<?php
if ($_GET["result"] == True)
     echo '<h1>Result: Success</h1>';
else
     echo '<h1>Result: Failed</h1>';
?>

<table class="history">
<tr>
<th>Test</th>
<th>Result</th>
</tr>


<?php

function lvfs_result($get_id, $title, $error_msg) {
     echo '<tr><td>' . $title . '</td><td>';
     if ($_GET[$get_id] == 'False')
          echo '&#x2610; ' . $error_msg;
     else
          echo '&#x2611; Passed';
     echo '</td></tr>';
}

lvfs_result('authkey', 'Auth Key', 'Did not match any registered vendors');
lvfs_result('sizecheck', 'Size Check', 'File was too small or large');
lvfs_result('filetype', 'File Type', 'Not a valid <code>cab</code> file');
lvfs_result('metadata', 'Metadata', 'The firmware file had no valid metadata');
lvfs_result('exists', 'Version Check', 'The firmware file already exists');

?>

</table>

<p>
 <a href="vendors.html">Go back to the submission page</a>.
</p>

<p class="footer">
 Copyright <a href="mailto:richard@hughsie.com">Richard Hughes 2015</a>
</p>

</body>

</html>

