#include <string.h>
#include <stdio.h>

#include <telepathy-glib/util.h>

#include <idle-text.h>
#include <idle-connection.h>

#define fail(message, ...) \
  G_STMT_START \
    { \
      fprintf (stderr, message "\n", ##__VA_ARGS__); \
      fprintf (stderr, "- msg: %s\n", g_strescape (msg, "")); \
      if (i >= 0) \
        fprintf (stderr, "- line #%d: %s\n", i, g_strescape (line, "")); \
      fprintf (stderr, "- type: %d\n", type); \
      return FALSE; \
    } \
  G_STMT_END


gboolean
test (TpChannelTextMessageType type,
      gchar *msg)
{
  gchar *recipient = "ircuser";
  gchar **output = idle_text_encode_and_split (type, recipient, msg, 510, NULL);
  GString *reconstituted_msg = g_string_sized_new (strlen (msg));
  int i = -1;
  char *line = NULL, *c = NULL;

  char *expected_prefixes[3] = {
    "PRIVMSG ",
    "PRIVMSG ",
    "NOTICE ",
  };

  char *expected_infixes[3] = {
    " :",
    " :\001ACTION ",
    " :",
  };

  char *expected_suffixes[3] = {
    "",
    "\001",
    "",
  };

  if (output == NULL)
    {
      fail ("total reality failure, idle_text_encode_and_split returned NULL");
    }

  for (i = 0; output[i] != NULL; i++)
    {
      line = output[i];
      c = line;

      if (strlen (line) > IRC_MSG_MAXLEN)
        {
          fail ("resulting line longer than maximum length %d", IRC_MSG_MAXLEN);
        }

      if (!g_str_has_prefix (c, expected_prefixes[type]))
        {
          fail ("resulting line missing prefix '%s'",
              g_strescape (expected_prefixes[type], ""));
        }
      c += strlen (expected_prefixes[type]);

      if (!g_str_has_prefix (c, recipient))
        {
          fail ("resulting line missing recipient");
        }
      c += strlen (recipient);

      if (!g_str_has_prefix (c, expected_infixes[type]))
        {
          fail ("resulting line missing infix '%s'",
              g_strescape (expected_infixes[type], ""));
        }
      c += strlen (expected_infixes[type]);

      if (!g_str_has_suffix (c, expected_suffixes[type]))
        {
          fail ("resulting line missing suffix '%s'",
              g_strescape (expected_suffixes[type], ""));
        }

      g_string_append_len (reconstituted_msg, c,
          strlen (c) - strlen (expected_suffixes[type]));
    }

  i = -1;

  {
    /* Remove newlines from original string; you can't tell whether the string
     * was split because of length or because of newlines, but as long as the
     * result is the same modulo newlines that's okay.
     */
    gchar **lines = g_strsplit (msg, "\n", 0);
    gchar *newlineless = g_strjoinv ("", lines);
    g_strfreev (lines);

    if (tp_strdiff (reconstituted_msg->str, newlineless))
      {
        fail ("recombining split message yielded a different string\n"
              "- result: \"%s\"\n- msg sans newlines: \"%s\"",
              reconstituted_msg->str, newlineless);
      }
  }

  return TRUE;
}


int
main (int argc,
      char **argv)
{
  gchar *msgs[] = {
      "This is a short message.",
      "This message\ncontains newlines.",
      "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four twenty-five twenty-six twenty-seven twenty-eight twenty-nine thirty thirty-one thirty-two thirty-three thirty-four thirty-five thirty-six thirty-seven thirty-eight thirty-nine forty forty-one forty-two forty-three forty-four forty-five forty-six forty-seven forty-eight forty-nine fifty fifty-one fifty-two fifty-three fifty-four fifty-five fifty-six fifty-seven fifty-eight fifty-nine sixty sixty-one sixty-two sixty-three sixty-four sixty-five sixty-six sixty-seven sixty-eight sixty-nine",
      "one two three four\nfive six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four twenty-five twenty-six twenty-seven twenty-eight twenty-nine thirty thirty-one thirty-two thirty-three thirty-four thirty-five thirty-six thirty-seven thirty-eight thirty-nine forty forty-one forty-two forty-three forty-four forty-five forty-six forty-seven forty-eight forty-nine fifty fifty-one fifty-two fifty-three fifty-four fifty-five fifty-six fifty-seven fifty-eight fifty-nine sixty sixty-one sixty-two sixty-three sixty-four sixty-five sixty-six sixty-seven sixty-eight sixty-nine",
      NULL
  };
  gboolean sad_face = FALSE;

  for (int i = 0; msgs[i] != NULL; i++)
    {
      for (TpChannelTextMessageType j = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
           j <= TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
           j++)
        {
          gboolean yay = test(j, msgs[i]);
          if (!yay)
            sad_face = TRUE;
        }
    }

  if (sad_face)
    {
      fprintf (stderr, "  :'(\n");
      return 1;
    }

  return 0;
}
