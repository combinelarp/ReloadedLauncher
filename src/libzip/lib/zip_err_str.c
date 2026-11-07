/*
  This file was generated automatically from zip.h and zipint.h.
  Do not edit by hand.
*/

#include "zipint.h"

#define L ZIP_ET_LIBZIP
#define N ZIP_ET_NONE
#define S ZIP_ET_SYS
#define Z ZIP_ET_ZLIB

#define E ZIP_DETAIL_ET_ENTRY
#define G ZIP_DETAIL_ET_GLOBAL

const struct _zip_err_info _zip_err_str[] = {
    { ZIP_ER_OK, "No error" },
    { ZIP_ER_MULTIDISK, "Multi-disk zip archives not supported" },
    { ZIP_ER_RENAME, "Renaming temporary file failed" },
    { ZIP_ER_CLOSE, "Closing zip archive failed" },
    { ZIP_ER_SEEK, "Seek error" },
    { ZIP_ER_READ, "Read error" },
    { ZIP_ER_WRITE, "Write error" },
    { ZIP_ER_CRC, "CRC error" },
    { ZIP_ER_ZIPCLOSED, "Containing zip archive was closed" },
    { ZIP_ER_NOENT, "No such file" },
    { ZIP_ER_EXISTS, "File already exists" },
    { ZIP_ER_OPEN, "Can't open file" },
    { ZIP_ER_TMPOPEN, "Failure to create temporary file" },
    { ZIP_ER_ZLIB, "Zlib error" },
    { ZIP_ER_MEMORY, "Malloc failure" },
    { ZIP_ER_CHANGED, "Entry has been changed" },
    { ZIP_ER_COMPNOTSUPP, "Compression method not supported" },
    { ZIP_ER_EOF, "Premature end of file" },
    { ZIP_ER_INVAL, "Invalid argument" },
    { ZIP_ER_NOZIP, "Not a zip archive" },
    { ZIP_ER_INTERNAL, "Internal error" },
    { ZIP_ER_INCONS, "Zip archive inconsistent" },
    { ZIP_ER_REMOVE, "Can't remove file" },
    { ZIP_ER_DELETED, "Entry has been deleted" },
    { ZIP_ER_ENCRNOTSUPP, "Encryption method not supported" },
    { ZIP_ER_RDONLY, "Read-only archive" },
    { ZIP_ER_NOPASSWD, "No password provided" },
    { ZIP_ER_WRONGPASSWD, "Wrong password provided" },
    { ZIP_ER_OPNOTSUPP, "Operation not supported" },
    { ZIP_ER_INUSE, "Resource still in use" },
    { ZIP_ER_TELL, "Tell error" },
    { ZIP_ER_COMPRESSED_DATA, "Compressed data invalid" },
    { ZIP_ER_CANCELLED, "Operation cancelled" },
    { ZIP_ER_DATA_LENGTH, "Unexpected length of data" },
    { ZIP_ER_NOT_ALLOWED, "Not allowed in torrentzip" },
    { ZIP_ER_TRUNCATED_ZIP, "Possibly truncated or corrupted zip archive" },
    { ZIP_ER_EF_TOO_LARGE, "Extra fields too large" },
};

const int _zip_err_str_count = sizeof(_zip_err_str)/sizeof(_zip_err_str[0]);

const struct _zip_err_info _zip_err_details[] = {
    { ZIP_ER_DETAIL_NO_DETAIL, "no detail" },
    { ZIP_ER_DETAIL_CDIR_OVERLAPS_EOCD, "central directory overlaps EOCD, or there is space between them" },
    { ZIP_ER_DETAIL_COMMENT_LENGTH_INVALID, "archive comment length incorrect" },
    { ZIP_ER_DETAIL_CDIR_LENGTH_INVALID, "central directory length invalid" },
    { ZIP_ER_DETAIL_CDIR_ENTRY_INVALID, "central header invalid" },
    { ZIP_ER_DETAIL_CDIR_WRONG_ENTRIES_COUNT, "central directory count of entries is incorrect" },
    { ZIP_ER_DETAIL_ENTRY_HEADER_MISMATCH, "local and central headers do not match" },
    { ZIP_ER_DETAIL_EOCD_LENGTH_INVALID, "wrong EOCD length" },
    { ZIP_ER_DETAIL_EOCD64_OVERLAPS_EOCD, "EOCD64 overlaps EOCD, or there is space between them" },
    { ZIP_ER_DETAIL_EOCD64_WRONG_MAGIC, "EOCD64 magic incorrect" },
    { ZIP_ER_DETAIL_EOCD64_MISMATCH, "EOCD64 and EOCD do not match" },
    { ZIP_ER_DETAIL_CDIR_INVALID, "invalid value in central directory" },
    { ZIP_ER_DETAIL_VARIABLE_SIZE_OVERFLOW, "variable size fields overflow header" },
    { ZIP_ER_DETAIL_INVALID_UTF8_IN_FILENAME, "invalid UTF-8 in filename" },
    { ZIP_ER_DETAIL_INVALID_UTF8_IN_COMMENT, "invalid UTF-8 in comment" },
    { ZIP_ER_DETAIL_INVALID_ZIP64_EF, "invalid Zip64 extra field" },
    { ZIP_ER_DETAIL_INVALID_WINZIPAES_EF, "invalid WinZip AES extra field" },
    { ZIP_ER_DETAIL_EF_TRAILING_GARBAGE, "garbage at end of extra fields" },
    { ZIP_ER_DETAIL_INVALID_EF_LENGTH, "extra field length is invalid" },
    { ZIP_ER_DETAIL_INVALID_FILE_LENGTH, "file length in header doesn't match actual file length" },
    { ZIP_ER_DETAIL_STORED_SIZE_MISMATCH, "compressed and uncompressed sizes don't match for stored file" },
    { ZIP_ER_DETAIL_DATA_DESCRIPTOR_MISMATCH, "local header and data descriptor do not match" },
    { ZIP_ER_DETAIL_EOCD64_LOCATOR_MISMATCH, "EOCD64 and EOCD64 locator do not match" },
    { ZIP_ER_DETAIL_UTF8_FILENAME_MISMATCH, "UTF-8 filename is ASCII and doesn't match filename" },
    { ZIP_ER_DETAIL_UTF8_COMMENT_MISMATCH, "UTF-8 comment is ASCII and doesn't match comment" },
    { ZIP_ER_DETAIL_COMPRESSED_DATA_TRAILING_GARBAGE, "garbage at end of compressed data" },
    { ZIP_ER_DETAIL_NUL_IN_FILENAME, "NUL byte in file name" },
    { ZIP_ER_DETAIL_MISSING_ZIP64_EF, "missing Zip64 extra field" },
};

const int _zip_err_details_count = sizeof(_zip_err_details)/sizeof(_zip_err_details[0]);