#include <check.h>
#include <stdlib.h>
#include <errno.h>

#include "../src/frame.h"

static frame_t *frame = NULL;

void setup()
{
	frame = frame_new();
}

void teardown()
{
	frame_free(frame);
	frame = NULL;
}

START_TEST(test_new)
{
	const char *cmd;
	const struct stomp_hdr *hdrs;
	const void *body;

	fail_if(frame == NULL, NULL);
	fail_if(frame_cmd_get(frame, &cmd), NULL);
	fail_if(frame_hdrs_get(frame, &hdrs), NULL);
	fail_if(frame_body_get(frame, &body), NULL);
}
END_TEST

START_TEST(test_cmd_set_frame_null)
{
	size_t len;
	const char *s;
	fail_if(frame == NULL, NULL);

	errno = 0;
	fail_unless(frame_cmd_set(NULL, "CONNECT") == -1, NULL);
	fail_unless(errno == EINVAL, NULL);
	
	len = frame_cmd_get(frame, &s);
	fail_if(len, NULL);
}
END_TEST

START_TEST(test_cmd_set_cmd_null)
{
	size_t len;
	const char *s;
	fail_if(frame == NULL, NULL);

	errno = 0;
	fail_unless(frame_cmd_set(frame, NULL) == -1, NULL);
	fail_unless(errno == EINVAL, NULL);
	
	len = frame_cmd_get(frame, &s);
	fail_if(len, NULL);
}
END_TEST

START_TEST(test_cmd_set_cmd_empty)
{
	size_t len;
	const char *s;
	fail_if(frame == NULL, NULL);

	errno = 0;
	fail_unless(frame_cmd_set(frame, "") == -1, NULL);
	fail_unless(errno == EINVAL, NULL);
	
	len = frame_cmd_get(frame, &s);
	fail_if(len, NULL);
}
END_TEST

START_TEST(test_cmd_set_cmd)
{
	const char *cmd = "CONNECT";
	
	fail_if(frame == NULL, NULL);

	fail_if(frame_cmd_set(frame, cmd) != 0, NULL);

	errno = 0;
	fail_if(frame_cmd_set(frame, cmd) != -1, NULL);
	fail_unless(errno == EINVAL, NULL);
}
END_TEST

START_TEST(test_cmd_set)
{
	const char *cmd = "CONNECT";
	const char *s;
	size_t len;

	fail_if(frame == NULL, NULL);

	fail_if(frame_cmd_set(frame, cmd) != 0, NULL);
	len = frame_cmd_get(frame, &s);
	fail_unless(len == strlen(cmd), NULL);
	fail_unless(!strncmp(cmd, s, len), NULL);
}
END_TEST


START_TEST(test_hdr_add_no_cmd)
{
	fail_if(frame == NULL, NULL);
	errno = 0;
	fail_unless(frame_hdr_add(frame, "version", "1.2") == -1, NULL);
	fail_unless(errno == EINVAL, NULL);

}
END_TEST

START_TEST(test_hdr_add_frame_null)
{
	const char *cmd = "CONNECT";

	fail_if(frame == NULL, NULL);
	fail_if(frame_cmd_set(frame, cmd) != 0, NULL);
	errno = 0;
	fail_unless(frame_hdr_add(NULL, "version", "1.2") == -1, NULL);
	fail_unless(errno == EINVAL, NULL);

}
END_TEST

START_TEST(test_hdr_add_key_null)
{
	const char *cmd = "CONNECT";

	fail_if(frame == NULL, NULL);
	fail_if(frame_cmd_set(frame, cmd) != 0, NULL);

	errno = 0;
	fail_unless(frame_hdr_add(frame, NULL, "1.2") == -1, NULL);
	fail_unless(errno == EINVAL, NULL);

}
END_TEST

START_TEST(test_hdr_add_val_null)
{
	const char *cmd = "CONNECT";

	fail_if(frame == NULL, NULL);
	fail_if(frame_cmd_set(frame, cmd) != 0, NULL);

	errno = 0;
	fail_unless(frame_hdr_add(frame, "version", NULL) == -1, NULL);
	fail_unless(errno == EINVAL, NULL);

}
END_TEST


START_TEST(test_hdr_add_key_empty)
{
	const char *cmd = "CONNECT";

	fail_if(frame == NULL, NULL);
	fail_if(frame_cmd_set(frame, cmd) != 0, NULL);

	errno = 0;
	fail_unless(frame_hdr_add(frame, "", "1.2") == -1, NULL);
	fail_unless(errno == EINVAL, NULL);

}
END_TEST

START_TEST(test_hdr_add_val_empty)
{
	const char *cmd = "CONNECT";

	fail_if(frame == NULL, NULL);
	fail_if(frame_cmd_set(frame, cmd) != 0, NULL);

	errno = 0;
	fail_unless(frame_hdr_add(frame, "version", "") == -1, NULL);
	fail_unless(errno == EINVAL, NULL);

}
END_TEST

START_TEST(test_hdr_add_body)
{
	fail_if(frame == NULL, NULL);
	fail_if(frame_cmd_set(frame, "CONNECT") != 0, NULL);
	fail_if(frame_body_set(frame, " ", 1) != 0, NULL);

	errno = 0;
	fail_unless(frame_hdr_add(frame, "version", "1.2") == -1, NULL);
	fail_unless(errno == EINVAL, NULL);

}
END_TEST

START_TEST(test_hdr_add)
{
	const struct stomp_hdr *hdrs;
	const char *key = "version";
	const char *val = "1.2";

	fail_if(frame == NULL, NULL);
	fail_if(frame_cmd_set(frame, "CONNECT") != 0, NULL);

	fail_if(frame_hdr_add(frame, key, val), NULL);

	fail_unless(frame_hdrs_get(frame, &hdrs) == 1, NULL);
	fail_if(strncmp(hdrs[0].key, key, strlen(key)), NULL);
	fail_if(strncmp(hdrs[0].val, val, strlen(val)), NULL);
}
END_TEST

START_TEST(test_hdrs_add_hdrs_null)
{
	fail_if(frame == NULL, NULL);
	
	errno = 0;
	fail_unless(frame_hdrs_add(frame, 0, NULL) == -1, NULL);
	fail_unless(errno == EINVAL, NULL);
}
END_TEST

START_TEST(test_hdrs_add)
{
	const char *key0 = "version";
	const char *val0 = "1.2";
	const char *key1 = "heart-beat";
	const char *val1 = "1000,1000";
	const struct stomp_hdr *hdrs0;
	const struct stomp_hdr hdrs[] = {
		{key0, val0},
		{key1, val1}
	};

	fail_if(frame == NULL, NULL);
	fail_if(frame_cmd_set(frame, "CONNECT") != 0, NULL);

	fail_if(frame_hdrs_add(frame, sizeof(hdrs)/sizeof(struct stomp_hdr), hdrs), NULL);

	fail_unless(frame_hdrs_get(frame, &hdrs0) == 2, NULL);
	fail_if(strncmp(hdrs0[0].key, key0, strlen(key0)), NULL);
	fail_if(strncmp(hdrs0[0].val, val0, strlen(val0)), NULL);
	fail_if(strncmp(hdrs0[1].key, key1, strlen(key1)), NULL);
	fail_if(strncmp(hdrs0[1].val, val1, strlen(val1)), NULL);
}
END_TEST

Suite *frame_suite()
{
	Suite *s = suite_create ("frame");

	TCase *tc_core = tcase_create ("core");
	tcase_add_checked_fixture (tc_core, setup, teardown);
	tcase_add_test(tc_core, test_new);
	tcase_add_test(tc_core, test_cmd_set);
	tcase_add_test(tc_core, test_cmd_set_cmd);
	tcase_add_test(tc_core, test_cmd_set_frame_null);
	tcase_add_test(tc_core, test_cmd_set_cmd_null);
	tcase_add_test(tc_core, test_cmd_set_cmd_empty);
	tcase_add_test(tc_core, test_hdr_add);
	tcase_add_test(tc_core, test_hdr_add_frame_null);
	tcase_add_test(tc_core, test_hdr_add_no_cmd);
	tcase_add_test(tc_core, test_hdr_add_key_null);
	tcase_add_test(tc_core, test_hdr_add_val_null);
	tcase_add_test(tc_core, test_hdr_add_key_empty);
	tcase_add_test(tc_core, test_hdr_add_val_empty);
	tcase_add_test(tc_core, test_hdr_add_body);
	tcase_add_test(tc_core, test_hdrs_add_hdrs_null);
	tcase_add_test(tc_core, test_hdrs_add);
	suite_add_tcase (s, tc_core);
	
	return s;
}

int main(int argc, const char *argv[])
{
	int number_failed;

	Suite *s = frame_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all (sr, CK_NORMAL);
	number_failed = srunner_ntests_failed (sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
