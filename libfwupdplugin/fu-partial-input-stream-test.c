/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_partial_input_stream_composite_func(void)
{
	gboolean ret;
	gint rc;
	guint8 buf[4] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("12345678", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) composite_stream = fu_composite_input_stream_new();
	g_autoptr(FuInputStream) partial_stream = NULL;

	ret = fu_composite_input_stream_add_bytes(FU_COMPOSITE_INPUT_STREAM(composite_stream),
						  blob,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* limit to '34' */
	partial_stream = fu_partial_input_stream_new(composite_stream, 2, 2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(partial_stream);

	/* seek to the start of the partial input stream */
	ret = g_seekable_seek(G_SEEKABLE(partial_stream), 0x0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x2);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(partial_stream)), ==, 0x0);

	/* read the 34 */
	rc = fu_input_stream_read(partial_stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, '3');
	g_assert_cmpint(buf[1], ==, '4');
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x4);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(partial_stream)), ==, 0x2);

	/* there is no more data to read */
	rc = fu_input_stream_read(partial_stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x4);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(partial_stream)), ==, 0x2);
}

static void
fu_partial_input_stream_simple_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[2] = {0x0};
	g_autoptr(GBytes) blob = g_bytes_new_static("12345678", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuInputStream) stream2 = NULL;

	/* use G_MAXSIZE for "rest of the stream" */
	stream = fu_partial_input_stream_new(base_stream, 4, G_MAXSIZE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x2, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x2);

	/* read from offset */
	rc = fu_input_stream_read(stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, '7');
	g_assert_cmpint(buf[1], ==, '8');

	/* overflow */
	stream2 = fu_partial_input_stream_new(base_stream, 4, G_MAXSIZE - 1, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(stream2);
}

static void
fu_partial_input_stream_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[5] = {0x0};
	goffset pos;
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("12345678", 8);
	/*                                             \--/   */
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream_complete = NULL;
	g_autoptr(FuInputStream) stream_error = NULL;
	g_autoptr(FuInputStream) stream_file = NULL;
	g_autoptr(FuInputStream) stream = NULL;

	/* check the behavior of FuFileInputStream */
	fn = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	g_assert_nonnull(fn);
	stream_file = fu_input_stream_from_path(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream_file);
	ret = g_seekable_seek(G_SEEKABLE(stream_file), 0x0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 0x0);
	ret = g_seekable_seek(G_SEEKABLE(stream_file), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 216);
	rc = fu_input_stream_read(stream_file, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	pos = g_seekable_tell(G_SEEKABLE(stream_file));
	g_assert_cmpint(pos, ==, 216);
	ret = g_seekable_seek(G_SEEKABLE(stream_file), pos, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = fu_input_stream_read(stream_file, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 216);
	/* we CAN seek past the end... */
	ret = g_seekable_seek(G_SEEKABLE(stream_file), pos + 10000, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 10216);
	/* reads all return zero */
	rc = fu_input_stream_read(stream_file, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	/* END offset is negative */
	ret = g_seekable_seek(G_SEEKABLE(stream_file), -0x1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = fu_input_stream_read(stream_file, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 10);

	/* check the behavior of FuMemoryInputStream */
	g_assert_no_error(error);
	g_assert_nonnull(stream_file);
	ret = g_seekable_seek(G_SEEKABLE(base_stream), 0x0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 0x0);
	ret = g_seekable_seek(G_SEEKABLE(base_stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 8);
	rc = fu_input_stream_read(base_stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	pos = g_seekable_tell(G_SEEKABLE(base_stream));
	g_assert_cmpint(pos, ==, 8);
	ret = g_seekable_seek(G_SEEKABLE(base_stream), pos, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = fu_input_stream_read(base_stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 8);
	/* we CANNOT seek past the end... */
	ret = g_seekable_seek(G_SEEKABLE(base_stream), pos + 10000, G_SEEK_SET, NULL, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
	g_clear_error(&error);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 8);
	/* END offset is negative */
	ret = g_seekable_seek(G_SEEKABLE(base_stream), -1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = fu_input_stream_read(base_stream, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, '8');

	/* seek to non-start */
	stream = fu_partial_input_stream_new(base_stream, 2, 4, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x2, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x2);

	/* read from start */
	rc = fu_input_stream_read(stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, '5');
	g_assert_cmpint(buf[1], ==, '6');
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x4);
	rc = fu_input_stream_read(stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* convert back to bytes */
	blob2 = fu_input_stream_read_bytes(stream, 0x0, G_MAXUINT32, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob2);
	g_assert_cmpint(g_bytes_get_size(blob2), ==, 4);

	/* seek to end of base stream */
	ret = g_seekable_seek(G_SEEKABLE(base_stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 0x8);
	rc = fu_input_stream_read(base_stream, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 0x8);

	/* seek to end of partial stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x4);
	rc = fu_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* seek to offset to end of partial stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), -1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x3);
	rc = fu_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, '6');

	/* attempt an overread of the base stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x2, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = fu_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);

	/* attempt to seek way past the base stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x1000, G_SEEK_SET, NULL, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
	g_clear_error(&error);

	/* read right up against the end of the base stream */
	stream_complete = fu_partial_input_stream_new(base_stream, 0, 8, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream_complete);
	ret = g_seekable_seek(G_SEEKABLE(stream_complete), 0x8, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = fu_input_stream_read(stream_complete, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* try to create an out-of-range partial stream */
	stream_error = fu_partial_input_stream_new(base_stream, 0, 9, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(stream_error);
}

static void
fu_partial_input_stream_shared_base_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[4] = {0};
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) partial1 = NULL;
	g_autoptr(FuInputStream) partial2 = NULL;

	/* create base stream and two partials, then drop the base */
	{
		g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
		g_autoptr(FuInputStream) base = fu_memory_input_stream_new_from_bytes(blob);

		partial1 = fu_partial_input_stream_new(base, 0, 4, &error);
		g_assert_no_error(error);
		g_assert_nonnull(partial1);

		partial2 = fu_partial_input_stream_new(base, 4, 4, &error);
		g_assert_no_error(error);
		g_assert_nonnull(partial2);
	}

	/* read from partial1 — base stream GObject is dead but the partial keeps it */
	ret = g_seekable_seek(G_SEEKABLE(partial1), 0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = fu_input_stream_read(partial1, buf, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 4);
	g_assert_cmpint(buf[0], ==, 'A');
	g_assert_cmpint(buf[1], ==, 'B');
	g_assert_cmpint(buf[2], ==, 'C');
	g_assert_cmpint(buf[3], ==, 'D');

	/* read from partial2 */
	ret = g_seekable_seek(G_SEEKABLE(partial2), 0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = fu_input_stream_read(partial2, buf, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 4);
	g_assert_cmpint(buf[0], ==, 'E');
	g_assert_cmpint(buf[1], ==, 'F');
	g_assert_cmpint(buf[2], ==, 'G');
	g_assert_cmpint(buf[3], ==, 'H');
}

static void
fu_partial_input_stream_nested_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[4] = {0};
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("0123456789", 10);
	g_autoptr(FuInputStream) outer = NULL;

	{
		g_autoptr(FuInputStream) base = fu_memory_input_stream_new_from_bytes(blob);

		/* create partial-of-partial, then drop the base and middle one */
		{
			g_autoptr(FuInputStream) middle = NULL;

			/* middle = "234567" */
			middle = fu_partial_input_stream_new(base, 2, 6, &error);
			g_assert_no_error(error);
			g_assert_nonnull(middle);

			/* outer = "45" */
			outer = fu_partial_input_stream_new(middle, 2, 2, &error);
			g_assert_no_error(error);
			g_assert_nonnull(outer);
		}
	}

	ret = g_seekable_seek(G_SEEKABLE(outer), 0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = fu_input_stream_read(outer, buf, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, '4');
	g_assert_cmpint(buf[1], ==, '5');
}

static void
fu_partial_input_stream_composite_lifetime_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[8] = {0};
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) composite = fu_composite_input_stream_new();

	/* add partial streams from two different base streams, both going out of scope */
	{
		g_autoptr(GBytes) blob1 = g_bytes_new_static("AAABBB", 6);
		g_autoptr(GBytes) blob2 = g_bytes_new_static("CCCDDD", 6);
		g_autoptr(FuInputStream) base1 = fu_memory_input_stream_new_from_bytes(blob1);
		g_autoptr(FuInputStream) base2 = fu_memory_input_stream_new_from_bytes(blob2);
		g_autoptr(FuInputStream) p1 = NULL;
		g_autoptr(FuInputStream) p2 = NULL;

		/* p1 = "AB" from base1, p2 = "CD" from base2 */
		p1 = fu_partial_input_stream_new(base1, 2, 2, &error);
		g_assert_no_error(error);
		g_assert_nonnull(p1);

		p2 = fu_partial_input_stream_new(base2, 2, 2, &error);
		g_assert_no_error(error);
		g_assert_nonnull(p2);

		ret = fu_composite_input_stream_add_partial_stream(
		    FU_COMPOSITE_INPUT_STREAM(composite),
		    FU_PARTIAL_INPUT_STREAM(p1),
		    &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		ret = fu_composite_input_stream_add_partial_stream(
		    FU_COMPOSITE_INPUT_STREAM(composite),
		    FU_PARTIAL_INPUT_STREAM(p2),
		    &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		/* base1, base2, p1, p2 all go out of scope */
	}

	ret = g_seekable_seek(G_SEEKABLE(composite), 0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = fu_input_stream_read(composite, buf, 8, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, 'A');
	g_assert_cmpint(buf[1], ==, 'B');

	rc = fu_input_stream_read(composite, buf, 8, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, 'C');
	g_assert_cmpint(buf[1], ==, 'D');

	/* EOF */
	rc = fu_input_stream_read(composite, buf, 8, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
}

static void
fu_partial_input_stream_same_base_composite_func(void)
{
	gboolean ret;
	guint8 buf[2] = {0};
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("0123456789", 10);
	g_autoptr(FuInputStream) base = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) composite = fu_composite_input_stream_new();
	g_autoptr(FuInputStream) p1 = NULL;
	g_autoptr(FuInputStream) p2 = NULL;
	g_autoptr(FuInputStream) p3 = NULL;

	/* create three non-overlapping partials from the same base */
	p1 = fu_partial_input_stream_new(base, 0, 3, &error);
	g_assert_no_error(error);
	g_assert_nonnull(p1);

	p2 = fu_partial_input_stream_new(base, 3, 3, &error);
	g_assert_no_error(error);
	g_assert_nonnull(p2);

	p3 = fu_partial_input_stream_new(base, 6, 4, &error);
	g_assert_no_error(error);
	g_assert_nonnull(p3);

	/* add all to composite */
	ret = fu_composite_input_stream_add_partial_stream(FU_COMPOSITE_INPUT_STREAM(composite),
							   FU_PARTIAL_INPUT_STREAM(p1),
							   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_composite_input_stream_add_partial_stream(FU_COMPOSITE_INPUT_STREAM(composite),
							   FU_PARTIAL_INPUT_STREAM(p2),
							   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_composite_input_stream_add_partial_stream(FU_COMPOSITE_INPUT_STREAM(composite),
							   FU_PARTIAL_INPUT_STREAM(p3),
							   &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* read within first sub-stream (p1 = "012") */
	ret = fu_input_stream_read_safe(composite, buf, 2, 0, 0, 2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, '0');
	g_assert_cmpint(buf[1], ==, '1');

	/* read within second sub-stream (p2 = "345") */
	ret = fu_input_stream_read_safe(composite, buf, 2, 0, 3, 2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, '3');
	g_assert_cmpint(buf[1], ==, '4');

	/* read within third sub-stream (p3 = "6789") */
	ret = fu_input_stream_read_safe(composite, buf, 2, 0, 8, 2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, '8');
	g_assert_cmpint(buf[1], ==, '9');
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/partial-input-stream", fu_partial_input_stream_func);
	g_test_add_func("/fwupd/partial-input-stream/simple", fu_partial_input_stream_simple_func);
	g_test_add_func("/fwupd/partial-input-stream/composite",
			fu_partial_input_stream_composite_func);
	g_test_add_func("/fwupd/partial-input-stream/shared-base",
			fu_partial_input_stream_shared_base_func);
	g_test_add_func("/fwupd/partial-input-stream/nested", fu_partial_input_stream_nested_func);
	g_test_add_func("/fwupd/partial-input-stream/composite-lifetime",
			fu_partial_input_stream_composite_lifetime_func);
	g_test_add_func("/fwupd/partial-input-stream/same-base-composite",
			fu_partial_input_stream_same_base_composite_func);
	return g_test_run();
}
