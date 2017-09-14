#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <regex.h>
// Only include if not already included
#if !defined (ASCIIFILE_INCLUDED)
#include "AsciiFile.h"
#endif

/*! @brief Enumerated types to be used for interpreting formats. */
enum mytypes { STRING, FLOAT, DOUBLE,
	       SHORTSHORT, SHORT, INT, LONG, LONGLONG,
	       USHORTSHORT, USHORT, UINT, ULONG, ULONGLONG };

/*!
  @brief Create a regex from a character array.
  Adapted from https://www.lemoda.net/c/unix-regex/
  @param[out] r pointer to regex_t. Resutling regex expression.
  @param[in] regex_text constant character pointer to text that should be
  compiled.
  @return static int Success or failure of compilation.
*/
static inline
int compile_regex (regex_t * r, const char * regex_text)
{
  int status = regcomp (r, regex_text, REG_EXTENDED);//|REG_NEWLINE);
  if (status != 0) {
    char error_message[LINE_SIZE_MAX];
    regerror (status, r, error_message, LINE_SIZE_MAX);
    printf ("Regex error compiling '%s': %s\n",
	    regex_text, error_message);
    return 1;
  }
  return 0;
};

/*!
  @brief Count the number of times a regular expression is matched in a string.
  @param[in] regex_text constant character pointer to string that should be
  compiled into a regex.
  @param[in] to_match constant character pointer to string that should be
  checked for matches.
  @return int Number of matches found. -1 is returned if the regex could not be
  compiled.
*/
static inline
int count_matches(const char *regex_text, const char *to_match) {
  int ret;
  int n_match = 0;
  regex_t r;
  // Compile
  ret = compile_regex(&r, regex_text);
  if (ret)
    return -1;
  // Loop until string done
  const char * p = to_match;
  const int n_sub_matches = 10;
  regmatch_t m[n_sub_matches];
  while (1) {
    int nomatch = regexec(&r, p, n_sub_matches, m, 0);
    if (nomatch)
      break;
    n_match++;
    p += m[0].rm_eo;
  }
  return n_match;
};

/*!
  @brief Make a replacement of regex matches, ignoring captured substrings.
  @param[in,out] buf Characer pointer to buffer that replacements should be
  made to.
  @param[in] len_buf const int length of buf.
  @param[in] re Constant character pointer to regex string.
  @param[in] rp Constant character pointer to the replacement text.
  @param[in] nreplace Constant int number of replacements to make. If 0, all
  matches are replaced.
  @return int -1 on failure if the regex could not be compiled or the buffer 
  is not big enough to contain the result. If succesful, the new length of buf
  is returned.
 */
static inline
int regex_replace_nosub(char *buf, const int len_buf,
			const char *re, const char *rp,
			const int nreplace) {
  /* printf("regex_replace_nosub(%s, %s, %s)\n", buf, re, rp); */
  // Compile
  regex_t r;
  int ret = compile_regex(&r, re);
  if (ret)
    return -1;
  // Loop making replacements
  int len_rp = strlen(rp);
  char * p = buf;
  const int ngroups = r.re_nsub + 1;
  regmatch_t *m = (regmatch_t*)malloc(ngroups * sizeof(regmatch_t));
  int len_m, rem_s, rem_l, delta_siz;
  int cur_pos = 0;
  int cur_siz = strlen(buf);
  int creplace = 0;
  while (1) {
    if ((nreplace > 0) && (creplace >= nreplace)) {
      /* printf("regex_replace_nosub: Maximum of %d replacements reached\n", */
      /* 	     creplace); */
      break;
    }
    int nomatch = regexec(&r, p, ngroups, m, 0);
    if (nomatch) {
      /* printf("regex_replace_nosub: nomatch for %s in %s\n", re, p); */
      break;
    }
    // Ensure replacement will not exceed buffer
    len_m = m[0].rm_eo - m[0].rm_so;
    delta_siz = len_rp - len_m;
    if ((cur_siz + delta_siz + 1) > len_buf) {
      printf("regex_replace_nosub: Relacement will exceed buffer.\n");
      cur_siz = -1;
      break;
    }
    // Move trailing
    rem_l = cur_siz - (cur_pos + m[0].rm_eo);
    rem_s = m[0].rm_so + len_rp;
    memmove(p + rem_s, p + m[0].rm_eo, rem_l + 1);
    // Copy replacement
    strcpy(p + m[0].rm_so, rp);
    // Advance
    p += rem_s;
    cur_pos += rem_s;
    cur_siz += delta_siz;
    creplace += 1;
  }
  /* printf("regex_replace_nosub() = %s\n", buf); */
  free(m);
  return cur_siz;
};


/*!
  @brief Extract substring references from a string.
  @param[in] buf Constant character pointer to buffer that references should be
  extracted from.
  @param[out] refs Pointer to pointer to memory where reference numbers should
  be stored. This function will reallocate it to fit the number of references
  returned. (Should be freed by calling program.)
  @return int Number of refs found. -1 indicates an error.
*/
static inline
int get_subrefs(const char *buf, int **refs) {
  // Compile
  regex_t r;
  int ret = compile_regex(&r, "\\$([[:digit:]])");
  if (ret)
    return -1;
  // Allocate;
  const int ngroups = r.re_nsub + 1;
  if (ngroups != 2) {
    printf("ERROR: regex could not find subgroup\n");
    return -1;
  }
  regmatch_t *m = (regmatch_t*)malloc(ngroups * sizeof(regmatch_t));
  // Prepare "bitmap"
  const int max_ref = 10; //99;
  int i;
  uint8_t *ref_bytes = (uint8_t*)malloc((max_ref + 1)*sizeof(uint8_t));
  for (i = 0; i <= max_ref; i++)
    ref_bytes[i] = 0;
  // Locate matches
  const char *p = buf;
  const int max_grp = 2;  // Digits in max_ref
  int igrp_len;
  char igrp[max_grp];
  int iref;
  while (1) {
    int nomatch = regexec(&r, p, ngroups, m, 0);
    if (nomatch) {
      break;
    }
    // Lone $ without digit
    /* printf("so = %d, eo = %d\n", m[1].rm_so, m[1].rm_eo); */
    if ((m[1].rm_so == -1) && (m[1].rm_eo == -1)) {
      p += m[0].rm_eo;
      continue;
    }
    // Substring
    igrp_len = m[1].rm_eo - m[1].rm_so;
    if (igrp_len > max_grp) {
      printf("Number longer than %d digits unlikely.\n", max_grp);
      free(m);
      free(ref_bytes);
      return -1;
    }
    strncpy(igrp, p + m[1].rm_so, igrp_len);
    igrp[igrp_len] = 0;
    // Extract ref number
    iref = atoi(igrp);
    if (iref > max_ref) {
      printf("Reference to substr %d exceeds limit (%d)\n", iref, max_ref);
      free(m);
      free(ref_bytes);
      return -1;
    }
    ref_bytes[iref] = 1;
    p += m[0].rm_eo;
  }
  // Get unique refs
  int nref = 0;
  for (i = 0; i <= max_ref; i++) {
    if (ref_bytes[i])
      nref++;
  }
  *refs = (int*)realloc(*refs, nref*sizeof(int));
  int ir;
  for (i = 0, ir = 0; i <= max_ref; i++) {
    if (ref_bytes[i]) {
      (*refs)[ir] = i;
      ir++;
    }
  }
  free(m);
  free(ref_bytes);
  // printf("%d refs in %s\n", nref, buf);
  return nref;
}


/*!
  @brief Make a replacement of regex matches, allowing for captured substrings.
  @param[in,out] buf Characer pointer to buffer that replacements should be
  made to.
  @param[in] len_buf const int length of buf.
  @param[in] re Constant character pointer to regex string.
  @param[in] rp Constant character pointer to the replacement text.
  @return int -1 on failure if the regex could not be compiled or the buffer 
  is not big enough to contain the result. If succesful, the new length of buf
  is returned.
 */
static inline
int regex_replace_sub(char *buf, const int len_buf,
		      const char *re, const char *rp) {
  // Compile
  regex_t r;
  int ret = compile_regex(&r, re);
  if (ret)
    return -1;
  // Loop making replacements
  char * p = buf;
  const int ngroups = r.re_nsub + 1;
  regmatch_t *m = (regmatch_t*)malloc(ngroups * sizeof(regmatch_t));
  char rp_sub[2*len_buf];
  char re_sub[len_buf];
  char igrp[len_buf];
  int len_m, rem_s, rem_l, delta_siz, len_rp;
  int cur_pos = 0;
  int cur_siz = strlen(buf);
  int creplace = 0;
  int i, j;
  while (1) {
    int nomatch = regexec(&r, p, ngroups, m, 0);
    if (nomatch) {
      /* printf("regex_replace_sub: nomatch for %s in %s\n", re, p); */
      break;
    }
    // Get list of subrefs
    int *refs = NULL;
    int nref = get_subrefs(rp, &refs);
    if (nref < 0) {
      printf("Error gettings subrefs\n");
      cur_siz = -1;
      break;
    }
    // For each subref complete replacements
    strcpy(rp_sub, rp);
    for (j = 0; j < nref; j++) {
      i = refs[j];
      strcpy(igrp, p + m[i].rm_so);
      igrp[m[i].rm_eo - m[i].rm_so] = 0; // terminate
      sprintf(re_sub, "\\$%d", i);
      ret = regex_replace_nosub(rp_sub, 2*len_buf, re_sub, igrp, 0);
      if (ret < 0) {
	printf("regex_replace_sub: Error replacing substring $%d.\n", i);
	free(m);
	return -1;
      }
    }
    // Ensure replacement will not exceed buffer
    len_rp = ret;
    len_m = m[0].rm_eo - m[0].rm_so;
    delta_siz = len_rp - len_m;
    if ((cur_siz + delta_siz + 1) > len_buf) {
      printf("regex_replace_sub: Relacement will exceed buffer.\n");
      cur_siz = -1;
      break;
    }
    // Move trailing
    rem_l = cur_siz - (cur_pos + m[0].rm_eo);
    rem_s = m[0].rm_so + len_rp;
    memmove(p + rem_s, p + m[0].rm_eo, rem_l);
    // Copy replacement
    strncpy(p + m[0].rm_so, rp_sub, len_rp);
    // Advance
    p += m[0].rm_so + len_rp;
    cur_pos += m[0].rm_so + len_rp;
    cur_siz += delta_siz;
    creplace += 1;
  }
  free(m);
  return cur_siz;
};

/*!
  @brief Count how many % format specifiers there are in format string.
  Formats are found by counting the number of matches to the regular expression
  adapted from https://stackoverflow.com/questions/446285/validate-sprintf-format-from-input-field-with-regex
  @param[in] fmt_str constant character pointer to string that should be
  searched for format specifiers.
  @return int Number of format specifiers found.
*/
static inline
int count_formats(const char* fmt_str) {
  const char * fmt_regex = "%([[:digit:]]+\\$)?[+-]?([ 0]|\'.{1})?-?[[:digit:]]*(\\.[[:digit:]]+)?[lhjztL]*[bcdeEufFgGosxX]";
  int ret = count_matches(fmt_regex, fmt_str);
  /* printf("%d, %s\n", ret, fmt_str); */
  return ret;
};

/*! @brief Structure containing information about an ASCII table. */
typedef struct asciiTable_t {
  asciiFile_t f; //!< ASCII file structure.
  char format_str[LINE_SIZE_MAX]; //!< Format string for rows.
  char column[64]; //!< Character(s) used to seperate columns.
  int ncols; //!< Number of columns in the table.
  int *format_typ; //!< Array of ncols integers specifying column types.
  int *format_siz; //!< Array of ncols sizes for elements in each column.
  int row_siz; //!< Size of an entire row in bytes.
  int status; //!< Negative if format_str has not been set yet.
} asciiTable_t;

/*!
  @brief Open the file.
  @param[in] t asciiTable_t table structure.
  @return int 0 if opened successfully, -1 otherwise.
*/
static inline
int at_open(asciiTable_t *t) {
  return af_open(&((*t).f));
};

/*!
  @brief Close the file.
  @param[in] t asciiTable_t table structure.
  @return int 0 if ocloseded successfully, -1 otherwise.
*/
static inline
void at_close(asciiTable_t *t) {
  af_close(&((*t).f));
};

/*!
  @brief Read a line from the file and parse it.
  @param[in] t constant asciiTable_t table structure.
  @param[out] ap va_list Pointers to variables where parsed arguments should be
  stored.
  @return int On success, the number of characters read. -1 on failure.
 */
static inline
int at_vreadline(const asciiTable_t t, va_list ap) {
  int ret = 0, com = 1;
  size_t nread = LINE_SIZE_MAX;
  char *line = (char*)malloc(nread);
  while ((ret >= 0) && (com == 1)) {
    ret = af_readline_full(t.f, &line, &nread);
    if (ret < 0)
      return ret;
    com = af_is_comment(t.f, line);
  }
  int sret = vsscanf(line, t.format_str, ap);
  if (sret != t.ncols) {
    printf("at_vreadline: %d arguments filled, but %d were expected\n",
	   sret, t.ncols);
    ret = -1;
  }
  free(line);
  return ret;
};

/*!
  @brief Format arguments to form a line and write it to the file.
  @param[in] t constant asciiTable_t table structure.
  @param[out] ap va_list Variables that should be formatted using the format
  string to create a line in the table.
  @return int On success, the number of characters written. -1 on failure.
 */
static inline
int at_vwriteline(const asciiTable_t t, va_list ap) {
  int ret = vfprintf(t.f.fd, t.format_str, ap);
  return ret;
};

/*!
  @brief Read a line from the file and parse it.
  @param[in] t constant asciiTable_t table structure.
  @param[out] ... Pointers to variables where parsed arguments should be
  stored.
  @return int On success, the number of characters read. -1 on failure.
 */
static inline
int at_readline(const asciiTable_t t, ...) {
  va_list ap;
  va_start(ap, t); // might need to use last element in structure
  int ret = at_vreadline(t, ap);
  va_end(ap);
  return ret;
};

/*!
  @brief Format arguments to form a line and write it to the file.
  @param[in] t constant asciiTable_t table structure.
  @param[out] ... Variables that should be formatted using the format
  string to create a line in the table.
  @return int On success, the number of characters written. -1 on failure.
 */
static inline
int at_writeline(const asciiTable_t t, ...) {
  va_list ap;
  va_start(ap, t);
  int ret = at_vwriteline(t, ap);
  va_end(ap);
  return ret;
};

/*!
  @brief Write the format string the the file, prepending it with a comment.
  @param[in] t constant asciiTable_t table structure.
  @return int On success, the number of characters written. -1 on failure.
 */
static inline
int at_writeformat(const asciiTable_t t) {
  int ret;
  if (af_is_open(t.f) == 1) {
    ret = fwrite(t.f.comment, 1, strlen(t.f.comment), t.f.fd);
    if (ret < 0)
      return ret;
  }
  ret = af_writeline_full(t.f, t.format_str);
  return ret;
};

/*!
  @brief Try to find the format string in the file.
  The format string is assumed to start with a comment.
  @param[in] t constant asciiTable_t table structure.
  @return 0 on success, -1 on failure.
 */
static inline
int at_discover_format_str(asciiTable_t *t) {
  int ret = at_open(t);
  if (ret < 0)
    return ret;
  size_t nread = LINE_SIZE_MAX;
  char *line = (char*)malloc(nread);
  ret = -1;
  while (getline(&line, &nread, (*t).f.fd) >= 0) {
    if (af_is_comment((*t).f, line) == 1) {
      if (count_formats(line) > 0) {
  	strcpy((*t).format_str, line + strlen((*t).f.comment));
  	ret = 0;
  	break;
      }
    }
  }
  at_close(t);
  free(line);
  return ret;
};

/*!
  @brief Set the number of columns by counting the format specifiers.
  @param[in] t constant asciiTable_t table structure.
  @return int The number of columns counted. Negative values indicate errors.
 */
static inline
int at_set_ncols(asciiTable_t *t) {
  // Assumes that format_str already done
  int count;
  count = count_formats((*t).format_str);
  (*t).ncols = count;
  return count;
};

/*!
  @brief Determine the column types by parsing the format string.
  @param[in] t constant asciiTable_t table structure.
  @return int 0 on success, -1 on failure.
  TODO: switch to regex
 */
static inline
int at_set_format_typ(asciiTable_t *t) {
  (*t).format_typ = (int*)malloc((*t).ncols*sizeof(int));
  (*t).format_siz = (int*)malloc((*t).ncols*sizeof(int));
  size_t beg = 0, end;
  int icol = 0;
  char ifmt[100];
  (*t).row_siz = 0;
  while (beg < strlen((*t).format_str)) {
    if ((*t).format_str[beg] == '%') {
      end = beg;
      while (end < strlen((*t).format_str)) {
	// Advance end to next column separator or new line
	if ((strncmp((*t).format_str + end, (*t).column, strlen((*t).column)) == 0) ||
	    (strncmp((*t).format_str + end, (*t).f.newline, strlen((*t).f.newline)) == 0)) {
	  strncpy(ifmt, &((*t).format_str)[beg], end-beg);
	  ifmt[end-beg] = '\0';
	  if ((*t).format_str[end-1] == 's') {
	    // String (variable length)
	    (*t).format_typ[icol] = STRING;
	    char len_fmt[100];
	    strncpy(len_fmt, &((*t).format_str)[beg+1], end-beg-2);
	    sscanf(len_fmt, "%d", &((*t).format_siz[icol]));
	  } else if (((*t).format_str[end-1] == 'f') ||
		     ((*t).format_str[end-1] == 'e') ||
		     ((*t).format_str[end-1] == 'E') ||
		     ((*t).format_str[end-1] == 'g') ||
		     ((*t).format_str[end-1] == 'G')) {
	    (*t).format_typ[icol] = DOUBLE;
	    (*t).format_siz[icol] = sizeof(double);
	    // Hack to allow double to be specified
	    /* if ((*t).format_str[end-2] == 'l') { */
	    /*   (*t).format_typ[icol] = DOUBLE; */
	    /*   (*t).format_siz[icol] = sizeof(double); */
	    /* } else { */
	    /*   (*t).format_typ[icol] = FLOAT; */
	    /*   (*t).format_siz[icol] = sizeof(float); */
	    /* } */
	  } else if (((*t).format_str[end-1] == 'd') ||
		     ((*t).format_str[end-1] == 'i')) {
	    // Integers
	    if ((*t).format_str[end-2] == 'h') {
	      if ((*t).format_str[end-3] == 'h') {
		(*t).format_typ[icol] = SHORTSHORT;
		(*t).format_siz[icol] = sizeof(char);
	      } else {
		(*t).format_typ[icol] = SHORT;
		(*t).format_siz[icol] = sizeof(short);
	      }
	    } else if ((*t).format_str[end-2] == 'l') {
	      if ((*t).format_str[end-3] == 'l') {
		(*t).format_typ[icol] = LONGLONG;
		(*t).format_siz[icol] = sizeof(long long);
	      } else {
		(*t).format_typ[icol] = LONG;
		(*t).format_siz[icol] = sizeof(long);
	      }
	    } else {
	      (*t).format_typ[icol] = INT;
	      (*t).format_siz[icol] = sizeof(int);
	    }
	  } else if (((*t).format_str[end-1] == 'u') ||
		     ((*t).format_str[end-1] == 'o') ||
		     ((*t).format_str[end-1] == 'x') ||
		     ((*t).format_str[end-1] == 'X')) {
	    // Unsigned integers
	    if ((*t).format_str[end-2] == 'h') {
	      if ((*t).format_str[end-3] == 'h') {
		(*t).format_typ[icol] = USHORTSHORT;
		(*t).format_siz[icol] = sizeof(unsigned char);
	      } else {
		(*t).format_typ[icol] = USHORT;
		(*t).format_siz[icol] = sizeof(unsigned short);
	      }
	    } else if ((*t).format_str[end-2] == 'l') {
	      if ((*t).format_str[end-3] == 'l') {
		(*t).format_typ[icol] = ULONGLONG;
		(*t).format_siz[icol] = sizeof(unsigned long long);
	      } else {
		(*t).format_typ[icol] = ULONG;
		(*t).format_siz[icol] = sizeof(unsigned long);
	      }
	    } else {
	      (*t).format_typ[icol] = UINT;
	      (*t).format_siz[icol] = sizeof(unsigned int);
	    }
	  } else {
	    printf("Could not parse format string: %s\n", ifmt);
	    return -1;
	  }
	  /* printf("%d: %s, typ = %d, siz = %d\n", icol, ifmt, */
	  /* 	 (*t).format_typ[icol], (*t).format_siz[icol]); */
	  (*t).row_siz += (*t).format_siz[icol];
	  icol++;
	  break;
	}
	end++;
      }
    }
    beg++;
  }
  return 0;
};

/*!
  @brief Convert data into arrays for columns.
  @param[in] t constant asciiTable_t table structure.
  @param[in] data constant character pointer to memory containing data that
  should be parsed.
  @param[in] data_siz constant int Size of data in bytes.
  @param[out] ap va_list Pointers to pointers to memory where columns should
  be stored.
  @return int Number of rows read on success, -1 on failure.
 */
static inline
int at_vbytes_to_array(const asciiTable_t t, const char *data,
		       const int data_siz, va_list ap) {
  // check size of array
  /* int data_siz = strlen(data); */
  if ((data_siz % t.row_siz) != 0) {
    printf("Data size (%d) not an even number of rows (row size is %d)\n",
	   data_siz, t.row_siz);
    return -1;
  }
  // Loop through
  int nrows = data_siz / t.row_siz;
  int cur_pos = 0, col_siz;
  int i;
  for (i = 0; i < t.ncols; i++) {
    char **temp;
    col_siz = nrows*t.format_siz[i];
    temp = va_arg(ap, char**);
    *temp = (char*)malloc(col_siz);
    // C order memory
    /* for (int j = 0; j < nrows; j++) { */
    /*   memcpy(*temp + j*t.format_siz[i], data + j*t.row_siz + cur_pos, t.format_siz[i]); */
    /* } */
    /* cur_pos += t.format_siz[i]; */
    // F order memory
    memcpy(*temp, data+cur_pos, col_siz);
    cur_pos += col_siz;
    /* printf("col %d: cur_pos = %d, col_siz = %d, data = %s, raw_data = ", i, cur_pos, col_siz, *temp); */
    /* fwrite(*temp, col_siz, 1, stdout); */
    /* printf("\n"); */
  }
  return nrows;
};

/*!
  @brief Encode a set of arrays as bytes.
  @param[in] t constant asciiTable_t table structure.
  @param[out] data Pointer to pointer to memory where encoded arrays should be
  stored. It does not need to be allocate, only declared.
  @param[in] nrows int Number of rows in each column array.
  @param[in] ap va_list Pointers to memory where column data is stored.
 */
static inline
int at_varray_to_bytes(const asciiTable_t t, char **data, int nrows, va_list ap) {
  // Allocate
  *data = (char*)realloc(*data, nrows*t.row_siz);
  // Loop through
  int cur_pos = 0, col_siz;
  char *temp;
  int i;
  for (i = 0; i < t.ncols; i++) {
    col_siz = nrows*t.format_siz[i];
    temp = va_arg(ap, char*);
    memcpy(*data+cur_pos, temp, col_siz);
    cur_pos += col_siz;
  }
  return cur_pos;
};

/*!
  @brief Convert data into arrays for columns.
  @param[in] t constant asciiTable_t table structure.
  @param[in] data constant character pointer to memory containing data that
  should be parsed.
  @param[in] data_siz constant int Size of data in bytes.
  @param[out] ... Pointers to pointers to memory where columns should
  be stored.
  @return int Number of rows read on success, -1 on failure.
 */
static inline
int at_bytes_to_array(const asciiTable_t t, char *data, int data_siz, ...) {
  va_list ap;
  va_start(ap, data_siz);
  int ret = at_vbytes_to_array(t, data, data_siz, ap);
  va_end(ap);
  return ret;
};

/*!
  @brief Encode a set of arrays as bytes.
  @param[in] t constant asciiTable_t table structure.
  @param[out] data Pointer to pointer to memory where encoded arrays should be
  stored. It does not need to be allocate, only declared.
  @param[in] nrows int Number of rows in each column array.
  @param[in] ... Pointers to memory where column data is stored.
 */
static inline
int at_array_to_bytes(const asciiTable_t t, char **data, int nrows, ...) {
  va_list ap;
  va_start(ap, nrows);
  int ret = at_varray_to_bytes(t, data, nrows, ap);
  va_end(ap);
  return ret;
};

/*!
  @brief Deallocate and clean up asciiTable_t structure.
  @param[in] t asciiTable_t table structure.
*/
static inline
void at_cleanup(asciiTable_t *t) {
  if ((*t).format_typ)
    free((*t).format_typ);
  if ((*t).format_siz)
    free((*t).format_siz);
  (*t).format_typ = NULL;
  (*t).format_siz = NULL;
};

/*!
  @brief Constructor for asciiTable_t structure.
  @param[in] filepath constant character pointer to file path.
  @param[in] io_mode constant character pointer to I/O mode. "r" for read,
  "w" for write.
  @param[in] format_str constant character pointer to string describing the
  format of the table roads. Required for io_mode == "w", but if set to NULL
  for io_mode == "r", it will attempt to be read from the table.
  @param[in] comment const character pointer to character(s) that should
  indicate a comment. If NULL, comment is set to "# ".
  @param[in] column const character pointer to character(s) that should
  separate columns in the table. If NULL, column is set to "\t".
  @param[in] newline const character pointer to character(s) that should
  indicate a newline. If NULL, newline is set to "\n".
  @return asciiTable_t table structure.
*/
static inline
asciiTable_t asciiTable(const char *filepath, const char *io_mode,
			const char *format_str, const char *comment,
			const char *column, const char *newline) {
  asciiTable_t t;
  strcpy(t.format_str, "\0");
  t.ncols = 0;
  t.format_typ = NULL;
  t.format_siz = NULL;
  t.row_siz = 0;
  t.status = 0;
  t.f = asciiFile(filepath, io_mode, comment, newline);
  // Set defaults for optional parameters
  if (column == NULL)
    strcpy(t.column, "\t");
  else
    strcpy(t.column, column);
  // Guess format string from file
  if (format_str == NULL) {
    if (strcmp(io_mode, "r") == 0) {
      t.status = at_discover_format_str(&t);
    } else {
      t.status = -1;
    }
  } else {
    strcpy(t.format_str, format_str);
  }
  // Get number of columns & types
  if (t.status >= 0)
    t.status = at_set_ncols(&t);
  if (t.status >= 0)
    t.status = at_set_format_typ(&t);
  /* printf("status = %d\n", t.status); */
  /* printf("format_str = %s\n", t.format_str); */
  /* printf("ncols = %d, row_siz = %d\n", t.ncols, t.row_siz); */
  return t;
};
