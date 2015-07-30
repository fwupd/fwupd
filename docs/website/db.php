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

function lvfs_connect_db() {
	return new mysqli($_ENV["OPENSHIFT_MYSQL_DB_HOST"],
			  $_ENV["OPENSHIFT_MYSQL_DB_USERNAME"],
			  $_ENV["OPENSHIFT_MYSQL_DB_PASSWORD"],
			  "beta",
			  (int) $_ENV["OPENSHIFT_MYSQL_DB_PORT"]);
}

function lvfs_disconnect_db($db) {
	$db->close();
}

function lvfs_check_exists($db, $auth_token) {
	if ($auth_token == '')
		return False;
	if (!($stmt = $db->prepare("SELECT * FROM users WHERE guid=?;")))
		die("failed to prepare: " . $db->error);
	$stmt->bind_param("s", $auth_token);
	if (!$stmt->execute())
		die("failed to execute: " . $db->error);
	$res = $stmt->get_result();
	$stmt->close();
	if ($res->num_rows > 0)
		return True;
	return False;
}

function lvfs_check_auth($db, $auth_token) {
	if ($auth_token == '')
		return False;
	if (!($stmt = $db->prepare("SELECT * FROM users WHERE guid=? AND state=1;")))
		die("failed to prepare: " . $db->error);
	$stmt->bind_param("s", $auth_token);
	if (!$stmt->execute())
		die("failed to execute: " . $db->error);
	$res = $stmt->get_result();
	$stmt->close();
	if ($res->num_rows > 0)
		return True;
	return False;
}

function lvfs_check_auth_master($db, $auth_token) {
	$master_update_contact = 'sign@fwupd.org';
	if ($auth_token == '')
		return False;
	if (!($stmt = $db->prepare("SELECT * FROM users WHERE update_contact = ? AND guid = ?;")))
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

?>
