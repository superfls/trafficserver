#include "misc.h"
#include <string.h>
#include <ts/ts.h>
#include "debug_macros.h"

voidpf
gzip_alloc(voidpf opaque, uInt items, uInt size)
{
  return (voidpf) TSmalloc(items * size);
}

void
gzip_free(voidpf opaque, voidpf address)
{
  TSfree(address);
}

void
normalize_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc)
{
  TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  int deflate = 0;
  int gzip = 0;

  //remove the accept encoding field(s), 
  //while finding out if gzip or deflate is supported.    
  while (field) {
    TSMLoc tmp;

    if (!deflate && !gzip) {
      int value_count = TSMimeHdrFieldValuesCount(reqp, hdr_loc, field);

      while (value_count > 0) {
        int val_len = 0;
        const char *val;

        --value_count;
        val = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, field, value_count, &val_len);

        if (val_len == strlen("gzip"))
          gzip = !strncmp(val, "gzip", val_len);
        else if (val_len == strlen("deflate"))
          deflate = !strncmp(val, "deflate", val_len);
      }
    }

    tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);
    TSMimeHdrFieldDestroy(reqp, hdr_loc, field);        //catch retval?
    TSHandleMLocRelease(reqp, hdr_loc, field);
    field = tmp;
  }

  //append a new accept-encoding field in the header
  if (deflate || gzip) {
    TSMimeHdrFieldCreate(reqp, hdr_loc, &field);
    TSMimeHdrFieldNameSet(reqp, hdr_loc, field, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);

    if (gzip) {
      TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, field, -1, "gzip", strlen("gzip"));
      info("normalized accept encoding to gzip");
    } else if (deflate) {
      TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, field, -1, "deflate", strlen("deflate"));
      info("normalized accept encoding to deflate");
    }

    TSMimeHdrFieldAppend(reqp, hdr_loc, field);
    TSHandleMLocRelease(reqp, hdr_loc, field);
  }
}

void
hide_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc)
{
  TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  while (field) {
    TSMLoc tmp;
    tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);
    TSMimeHdrFieldNameSet(reqp, hdr_loc, field, hidden_header_name, -1);
    TSHandleMLocRelease(reqp, hdr_loc, field);
    field = tmp;
  }
}

void
restore_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc)
{
  TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, hidden_header_name, -1);

  while (field) {
    TSMLoc tmp;
    tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);
    TSMimeHdrFieldNameSet(reqp, hdr_loc, field, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
    TSHandleMLocRelease(reqp, hdr_loc, field);
    field = tmp;
  }
}

void
init_hidden_header_name()
{
  const char *var_name = "proxy.config.proxy_name";
  TSMgmtString result;

  if (TSMgmtStringGet(var_name, &result) != TS_SUCCESS) {
    fatal("failed to get server name");
  } else {
    int hidden_header_name_len = strlen("x-accept-encoding-") + strlen(result);
    hidden_header_name = (char *) TSmalloc(hidden_header_name_len + 1);
    hidden_header_name[hidden_header_name_len] = 0;
    sprintf(hidden_header_name, "x-accept-encoding-%s", result);
  }
}

int
check_ts_version()
{
  const char *ts_version = TSTrafficServerVersionGet();
  TSReleaseAssert(ts_version);

  int scan_result;
  int major_version;

  scan_result = sscanf(ts_version, "%d", &major_version);
  TSReleaseAssert(scan_result == 1);

  return major_version >= 3;
}

int
register_plugin()
{
  TSPluginRegistrationInfo info;

  info.plugin_name = "gzip";
  info.vendor_name = "Apache";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    return 0;
  }
  return 1;
}

const char *
load_dictionary(const char *preload_file)
{
  char *dict = (char *) malloc(800000);
  uLong dictId = adler32(0L, Z_NULL, 0);
  uLong *adler = &dictId;

  FILE *fp;
  int i = 0;

  fp = fopen(preload_file, "r");
  if (!fp) {
    fatal("gzip-transform: ERROR: Unable to open dict file %s", preload_file);
  }

  /* dict = (char *) calloc(8000, sizeof(char)); */

  i = 0;
  while (!feof(fp)) {
    if (fscanf(fp, "%s\n", dict + i) == 1) {
      i = strlen(dict);
      strcat(dict + i, " ");
      ++i;
    }
  }
  dict[i - 1] = '\0';

  /* TODO get the adler compute right */
  *adler = adler32(*adler, (const Byte *) dict, sizeof(dict));
  return dict;
}

void
gzip_log_ratio(int64_t in, int64_t out)
{
  if (in) {
    info("Compressed size %ld (bytes), Original size %ld, ratio: %f", out, in, ((float) (in - out) / in));
  } else {
    debug("Compressed size %ld (bytes), Original size %ld, ratio: %f", out, in, 0.0F);
  }
}
