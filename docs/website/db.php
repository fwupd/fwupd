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

?>
