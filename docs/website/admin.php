<?php

/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

include 'db.php';

function lvfs_check_auth_master($db, $auth_token) {
	$master_update_contact = 'sign@fwupd.org';
	if ($auth_token == '')
		return False;
	if (!($stmt = $db->prepare('SELECT * FROM users WHERE update_contact = ? AND guid = ?;')))
		die("failed to prepare: " . $db->error);
	$stmt->bind_param("ss", $master_update_contact, $auth_token);
	if (!$stmt->execute())
		die("failed to execute: " . $db->error);
	$res = $stmt->get_result();
	$stmt->close();
	if ($res->num_rows > 0)
		return True;
	return False;
}

function lvfs_admin_add($db, $auth_master, $auth_vendor, $name, $update_contact) {

	$success = True;
	$uri = 'result.php?';

	# check auth key
	if (!lvfs_check_auth_master($db, $auth_master)) {
		$success = False;
		$uri = $uri . 'authkey=False&';
	}

	# check the vendor key does not already exist
	$query = "SELECT * FROM users WHERE guid = ?;";
	if (!($stmt = $db->prepare($query)))
		die("failed to prepare: " . $db->error);
	$stmt->bind_param("s", $auth_vendor);
	if (!$stmt->execute())
		die("failed to execute: " . $stmt->error);
	$res = $stmt->get_result();
	$stmt->close();
	if ($res->num_rows > 0) {
		$success = False;
		$uri = $uri . 'exists=False&';
	}

	# only add if we passed all tests
	if ($success == True) {
		$query = "INSERT INTO users (guid, name, update_contact, state) " .
			 "VALUES (?, ?, ?, 1);";
		if (!($stmt = $db->prepare($query)))
			die("failed to prepare: " . $db->error);
		$stmt->bind_param("sss",
				  $auth_vendor,
				  $name,
				  $update_contact);
		if (!$stmt->execute())
			die("failed to execute: " . $stmt->error);
		$stmt->close();
	}

	return $uri . 'result=' . $success;
}

function lvfs_admin_remove($db, $auth_master, $auth_vendor) {

	$success = True;
	$uri = 'result.php?';

	# check auth key
	if (!lvfs_check_auth_master($db, $auth_master)) {
		$success = False;
		$uri = $uri . 'authkey=False&';
	}

	# only disable if we passed all tests
	if ($success == True) {
		$query = "UPDATE users SET state = 0 WHERE guid = ?;";
		if (!($stmt = $db->prepare($query)))
			die("failed to prepare: " . $db->error);
		$stmt->bind_param("s", $auth_vendor);
		if (!$stmt->execute())
			die("failed to execute: " . $stmt->error);
		$stmt->close();
	}

	return $uri . 'result=' . $success;
}

# connect to database and perform action
$db = lvfs_connect_db();
$location = 'index.html';
if ($_GET["action"] == 'add')
	$location = lvfs_admin_add($db, $_POST['auth_master'], $_POST['auth_vendor'], $_POST['name'], $_POST['update_contact']);
if ($_GET["action"] == 'disable')
	$location = lvfs_admin_remove($db, $_POST['auth_master'], $_POST['auth_vendor']);
lvfs_disconnect_db($db);

header('Location: ' . $location);

?>
