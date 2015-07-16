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

<h1>History</h1>

<table class="history">
<tr>
<th>Vendor</th>
<th>Filename</th>
<th>Hash</th>
</tr>

<?php

include 'db.php';

function lvfs_get_vendor_name($db, $id) {
	$res = $db->query('SELECT name FROM users WHERE guid = "' . $id . '";');
	return $res->fetch_assoc()['name'];
}

$db = lvfs_connect_db();
$res = $db->query('SELECT * FROM firmware');
while ($row = $res->fetch_assoc()) {
	$vendor_name = lvfs_get_vendor_name($db, $row["vendor_key"]);
	echo '<tr>';
	echo '<td>' . $vendor_name . '</td>';
	echo '<td>' . $row["filename"] . '</td>';
	echo '<td>' . $row["hash"] . '</td>';
	echo '</tr>';
}

lvfs_disconnect_db($db);

?>
</table>

<p class="footer">
 Copyright <a href="mailto:richard@hughsie.com">Richard Hughes 2015</a>
</p>

</body>

</html>

