#include <check.h>
#include <stdlib.h>

#include "../src/frame.h"

static frame_t *frame;

static void setup()
{
  frame = frame_new();
}

static void teardown()
{
  frame_free(frame);
}

START_TEST(test_frame_buflene)
{
	const char *data = "\r\n:\\x";
	size_t elen = buflene(data, strlen(data));
	fail_unless(elen == 9, "Escaped length of \"\\r\\n:\\\\x\" is 9 but got %d", elen);
}
END_TEST

START_TEST(test_frame_bufcat)
{
	void *buf;
	const char *data = "Hello world!";
	size_t len = strlen(data);
	const char *data0= data; /* "Hello "*/
	size_t len0 = 6; 
	const char *data1= data + len0; /* "world!" */
	size_t len1 = len - len0;

	fail_unless(frame->buf_len == 0, "Buffer length shall be 0 after initialization.");

	buf = frame_bufcat(frame, data0, len0);
	fail_unless(frame->buf_len == len0, "Buffer length shall increase by the amount concatenated to the buffer.");
	fail_unless(buf != NULL, "Destination buffer is not initialized.");
	fail_unless(strncmp(buf, data0, frame->buf_len) == 0, "Destination buffer shall point to the start of the concatenated data.");
	fail_unless(strncmp(frame->buf, data0, frame->buf_len) == 0, "Data shall be copied to the buffer after initialization.");

	buf = frame_bufcat(frame, data1, len1);
	fail_unless(frame->buf_len == len, "Buffer length shall increase by the amount concatenated to the buffer.");
	fail_unless(buf != NULL, "Destination buffer is not initialized.");
	fail_unless(strncmp(buf, data1, len1) == 0, "Destination buffer shall point to the start of the concatenated data.");
	fail_unless(strncmp(frame->buf, data, frame->buf_len) == 0, "Data shall be concatenated to the buffer.");
}
END_TEST

START_TEST(test_frame_bufcate)
{
	void *buf;
	const char *data = "\r\n:x\\x";
	const char *edata = "\\r\\n\\cx\\\\x";
	size_t len = strlen(data);
	size_t elen = strlen(edata);
	const char *data0= data; /* "\r\n:x"*/
	size_t len0 = 4; 
	size_t elen0 = 7;
	const char *data1= data + len0; /* "\\x" */
	size_t len1 = len - len0;
	size_t elen1 = elen - elen0;

	fail_unless(frame->buf_len == 0, "Buffer length shall be 0 after initialization.");

	buf = frame_bufcate(frame, data0, len0);
	fail_unless(frame->buf_len == elen0, "Buffer length shall increase by the escaped amount concatenated to the buffer.");
	fail_unless(buf != NULL, "Destination buffer is not initialized.");
	fail_unless(strncmp(buf, edata, frame->buf_len) == 0, "Destination buffer shall point to the start of the escaped concatenated data.");
	fail_unless(strncmp(frame->buf, edata, frame->buf_len) == 0, "Escaped data shall be copied to the buffer after initialization.");

	buf = frame_bufcate(frame, data1, len1);
	fail_unless(frame->buf_len == elen, "Buffer length shall increase by the escaped amount concatenated to the buffer.");
	fail_unless(buf != NULL, "Destination buffer is not initialized.");
	fail_unless(strncmp(buf, edata + elen0, elen1) == 0, "Destination buffer shall point to the start of the concatenated data.");
	fail_unless(strncmp(frame->buf, edata, frame->buf_len) == 0, "Data shall be concatenated to the buffer.");
}
END_TEST

Suite *frame_suite(void)
{
  Suite *s = suite_create ("frame");

  /* Core test case */
  TCase *tc_core = tcase_create ("core");
  tcase_add_test (tc_core, test_frame_buflene);
  suite_add_tcase (s, tc_core);
  
  /* STOMP frame test case */
  TCase *tc_stomp = tcase_create ("stomp");
  tcase_add_checked_fixture (tc_stomp, setup, teardown);
  tcase_add_test (tc_stomp, test_frame_bufcat);
  tcase_add_test (tc_stomp, test_frame_bufcate);
  suite_add_tcase (s, tc_stomp);

  return s;
}

int main(int argc, const char *argv[])
{
	int number_failed;

	Suite *s = frame_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
