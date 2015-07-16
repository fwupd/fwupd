Linux Vendor Firmware Service
=============================

This is the website for the Linux Vendor Firmware Service

IMPORTANT: This needs to be hosted over SSL, i.e. with a `https://` prefix.

Using
-----

Using `index.html` you can upload files to the upload directory.
You can also dump all the user-visible databases using `dump.php`.

Installation
------------

The default upload path of /var/www/html/lvfs/uploads needs to be writable by
the apache user. You might have to tweak your SELinux policy too.

We also need a SQL server somewhere, with the following tables set up:

    CREATE TABLE `firmware` (
      `vendor_key` varchar(36) DEFAULT NULL,
      `update_contact` varchar(255) DEFAULT NULL,
      `addr` varchar(16) DEFAULT NULL,
      `timestamp` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
      `filename` varchar(255) DEFAULT NULL,
      `hash` varchar(40) DEFAULT NULL
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;
    CREATE TABLE `users` (
      `guid` varchar(36) NOT NULL DEFAULT '',
      `name` varchar(128) DEFAULT NULL
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

Just modify the `db.php` file with your login credentials. You can insert
authorised vendors with:

INSERT INTO `users` (`guid`, `name`) VALUES
('06350563-5b58-4c1d-8959-d9a216188604', 'Vendor1'),
('579caa6c-29d3-4efa-8f4d-bd2ff46af798', 'Vendor2');
