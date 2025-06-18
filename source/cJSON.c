/*
  Copyright (c) 2009-2025 Dave Gamble and cJSON contributors
  Copyright 2025 EPIC Software Labs
  Responsible party for this fork: EPIC Software Labs (list@epicsol.org)

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/* cJSON -- RFC8259 support for C */

/* 
   This was forked 
   from:   https://github.com/DaveGamble/cJSON
   commit: Version 1.7.18+ (12c4bf1986c288950a3d06da757109a6aa1ece38)
   imported on 3/25/2025

   This is a fork of the original.
   It is almost completely rewritten, but is still a derivative work.
   Do not bother the original authors about problems with this fork.
   Contact EPIC Software Labs (list@epicsol.org) only.
 */
/*
 * If the original authors of this code ever happen to read this,
 * thank you for the great code you gave the world and making this
 * so easy to adapt/adopt. :) Come see us on #epic on EFNet!
 */

/* 
 * VOCABULARY USED IN THIS IMPLEMENTATION
 * To every extent possible, these words are used solely as they are
 * defined in https://datatracker.ietf.org/doc/html/rfc8259
 *	Value		
 *	Object		
 *	Member
 *	Name
 *	String
 *	Name-Seperator
 *	Value-Separator	
 *	Array		
 *	Number
 *	String
 *      Space
 *	Unescaped
 *	Escape
 *	Parser
 *	Generator
 *
 * Words specific to this implementation:
 *	Item
 *		An Item is a single instance of the (cJSON) struct
 *		An Item contains a Value and data structures to 
 *		represent how a Value is related to other Items
 *		within composite types (Array, Object)
 */
/*
 * This fork has many fewer features than the original version, which is 
 * because json support has a lot of considerations for the web that we do
 * not have.  The features we focus on are:
 *
 *  1) Deserialization ("Parsing") (string -> json objects)
 *  2) Serialization ("Generation") (json objects -> string)
 *  3) Compound Object management
 *
 * Other things that were removed might be re-added if we need them.
 * For our needs, we expect scalar json items to be immutable after being built.
 */
#include "irc_std.h"
#include "irc.h"
#include "ircaux.h"
#include "cJSON.h"

static	cJSON *	cJSON_NewItem 	(void);

/**********************************************************************/
/* Deserialize a JSON string into a cJSON tree */

/*
 * The cJSON_Parser is a byte array containing serialized JSON, 
 * along with our metadata about processing it.
 * All the parsing functions will pass this along as context.
 */
typedef struct
{
	const char *	content;
	size_t 		length;
	size_t		offset;
	size_t 		depth; 
} cJSON_Parser;

/* 
 * Are there still <size> bytes left to read in the input?
 * Note that <size> is counted from 1!
 */
#define can_read(buffer, size) 			(( buffer ) && ((( buffer )->offset + size) <= ( buffer )->length))

/*
 * Would the <index>th byte in the buffer be valid?
 * Note that <index> is counted from 0!
 */
#define can_access_at_index(buffer, index) 	(( buffer ) && ((( buffer )->offset + index) < ( buffer )->length))

/*
 * Would the <index>th byte in the buffer be out of bounds?
 * Note that <index> is counted from 0!
 */
#define cannot_access_at_index(buffer, index) 	(!can_access_at_index( buffer , index))

/*
 * Return a (char *) to the next byte in the input.
 */
#define buffer_at_offset(buffer) 		(( buffer )->content + ( buffer )->offset)

static cJSON_bool	cJSON_ParseValue (cJSON * item, cJSON_Parser *);

/* * * * */
/* I do not currently have a use case for this */
#if 0
static	cJSON_bool	cJSON_IsInt (double value)
{
	double	a;

        a = round(value);
        if (fabs(a - value) < DBL_EPSILON)
		if (a < 1e52 && a > -1e52)
			return true_;
        return false_;
}
#endif

/* Parse the input text to generate a number, and populate the result into item. */
static cJSON_bool	cJSON_ParseNumber (cJSON *item, cJSON_Parser *input_buffer)
{
	double		number = 0;
	char *		after_end = NULL;

	if (!input_buffer || !input_buffer->content)
		return false_;

	number = strtod(buffer_at_offset(input_buffer), &after_end);
	if (buffer_at_offset(input_buffer) == after_end)
		return false_; /* parse_error */

	item->valuedouble = number;
	item->type = cJSON_Number;

	input_buffer->offset += (ptrdiff_t)(after_end - buffer_at_offset(input_buffer));
	return true_;
}

/* parse 4 digit hexadecimal number */
static uint32_t	parse_hex4 (const char *input_)
{
const	char *		input = (const char *)input_;
	unsigned	h = 0;
	size_t		i = 0;

	for (i = 0; i < 4; i++)
	{
		/* parse digit */
		if ((input[i] >= '0') && (input[i] <= '9'))
			h += input[i] - '0';
		else if ((input[i] >= 'A') && (input[i] <= 'F'))
			h += ((input[i] - 'A') + 10);
		else if ((input[i] >= 'a') && (input[i] <= 'f'))
			h += ((input[i] - 'a') + 10);
		else /* invalid */
			return 0;

		if (i < 3)
			/* shift left to make place for the next nibble */
			h = h << 4;
	}

	return h;
}

/* 
 * converts a UTF-16 literal to UTF-8
 * A literal can be one or two sequences of the form \uXXXX 
 */
static	size_t	utf16_literal_to_utf8 (const char *input_pointer, const char *input_end, char **output_pointer)
{
	unsigned long	codepoint = 0;
	unsigned 	first_code = 0;
const 	char *		first_sequence = input_pointer;
	size_t		utf8_length = 0;
	size_t		utf8_position = 0;
	size_t		sequence_length = 0;
	unsigned char	first_byte_mark = 0;

	if ((input_end - first_sequence) < 6)
		/* input ends unexpectedly */
		goto fail;

	/* get the first utf16 sequence */
	first_code = parse_hex4(first_sequence + 2);

	/* check that the code is valid */
	if (((first_code >= 0xDC00) && (first_code <= 0xDFFF)))
		goto fail;

	/* UTF16 surrogate pair */
	if ((first_code >= 0xD800) && (first_code <= 0xDBFF))
	{
		const char *	second_sequence = first_sequence + 6;
		unsigned int	second_code = 0;

		sequence_length = 12; /* \uXXXX\uXXXX */

		if ((input_end - second_sequence) < 6)
			/* input ends unexpectedly */
			goto fail;

		if ((second_sequence[0] != '\\') || (second_sequence[1] != 'u'))
			/* missing second half of the surrogate pair */
			goto fail;

		/* get the second utf16 sequence */
		second_code = parse_hex4((const char *)second_sequence + 2);
		/* check that the code is valid */
		if ((second_code < 0xDC00) || (second_code > 0xDFFF))
			/* invalid second half of the surrogate pair */
			goto fail;


		/* calculate the unicode codepoint from the surrogate pair */
		codepoint = 0x10000 + (((first_code & 0x3FF) << 10) | (second_code & 0x3FF));
	}
	else
	{
		sequence_length = 6; /* \uXXXX */
		codepoint = first_code;
	}

	/* encode as UTF-8
	* takes at maximum 4 bytes to encode:
	* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
	if (codepoint < 0x80)
		/* normal ascii, encoding 0xxxxxxx */
		utf8_length = 1;
	else if (codepoint < 0x800)
	{
		/* two bytes, encoding 110xxxxx 10xxxxxx */
		utf8_length = 2;
		first_byte_mark = 0xC0; /* 11000000 */
	}
	else if (codepoint < 0x10000)
	{
		/* three bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx */
		utf8_length = 3;
		first_byte_mark = 0xE0; /* 11100000 */
	}
	else if (codepoint <= 0x10FFFF)
	{
		/* four bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx 10xxxxxx */
		utf8_length = 4;
		first_byte_mark = 0xF0; /* 11110000 */
	}
	else
		/* invalid unicode codepoint */
		goto fail;

	/* encode as utf8 */
	for (utf8_position = (unsigned char)(utf8_length - 1); utf8_position > 0; utf8_position--)
	{
		/* 10xxxxxx */
		(*output_pointer)[utf8_position] = (unsigned char)((codepoint | 0x80) & 0xBF);
		codepoint >>= 6;
	}
	/* encode first byte */
	if (utf8_length > 1)
		(*output_pointer)[0] = (unsigned char)((codepoint | first_byte_mark) & 0xFF);
	else
		(*output_pointer)[0] = (unsigned char)(codepoint & 0x7F);

	*output_pointer += utf8_length;

	return sequence_length;

fail:
	return 0;
}

/* Parse the input text into an unescaped cinput, and populate item. */
static cJSON_bool	cJSON_ParseString (cJSON *item, cJSON_Parser *input_buffer)
{
const 	char *		input_pointer 	= buffer_at_offset(input_buffer) + 1;
const 	char *		input_end 	= buffer_at_offset(input_buffer) + 1;
	char *		output_pointer 	= NULL;
	char *		output 		= NULL;

	/* not a string */
	if (buffer_at_offset(input_buffer)[0] != '\"')
		goto fail;

	{
	/* calculate approximate size of the output (overestimate) */
	size_t		allocation_length = 0;
	size_t		skipped_bytes = 0;

	while (((size_t)(input_end - input_buffer->content) < input_buffer->length) && (*input_end != '\"'))
	{
		/* is escape sequence */
		if (input_end[0] == '\\')
		{
			if ((size_t)(input_end + 1 - input_buffer->content) >= input_buffer->length)
				/* prevent buffer overflow when last input character is a backslash */
				goto fail;

			skipped_bytes++;
			input_end++;
		}
		input_end++;
	}

	if (((size_t)(input_end - input_buffer->content) >= input_buffer->length) || (*input_end != '\"'))
		goto fail; /* string ended unexpectedly */

	/* This is at most how much we need for the output */
	allocation_length = (size_t) (input_end - buffer_at_offset(input_buffer)) - skipped_bytes;
	if (!(output = (char *)new_malloc(allocation_length + sizeof(""))))
		goto fail; /* allocation failure */

	for (output_pointer = output; input_pointer < input_end; )
	{
		size_t	sequence_length = 2;

		if (*input_pointer != '\\')
		{
			*output_pointer++ = *input_pointer++;
			continue;
		}

		/* I'm sure the compiler will "optimize" this memory safety check away... */
		if ((input_end - input_pointer) < 1)
			goto fail;

		switch (input_pointer[1])
		{
		    case 'b':
			*output_pointer++ = '\b';
			break;
		    case 'f':
			*output_pointer++ = '\f';
			break;
		    case 'n':
			*output_pointer++ = '\n';
			break;
		    case 'r':
			*output_pointer++ = '\r';
			break;
		    case 't':
			*output_pointer++ = '\t';
			break;
		    case '\"':
		    case '\\':
		    case '/':
			*output_pointer++ = input_pointer[1];
			break;

		    /* UTF-16 literal */
		    case 'u':
			if (!(sequence_length = utf16_literal_to_utf8(input_pointer, input_end, &output_pointer)))
				goto fail;
			break;

		    default:
			goto fail;
		}
		input_pointer += sequence_length;
	}

	/* zero terminate the output */
	*output_pointer = 0;

	item->type = cJSON_String;
	item->valuestring = (char*)output;

	input_buffer->offset = (size_t) (input_end - input_buffer->content);
	input_buffer->offset++;

	return true_;
	}

fail:
	if (output)
	{
		new_free(&output);
		output = NULL;
	}

	if (input_pointer != NULL)
		input_buffer->offset = (size_t)(input_pointer - input_buffer->content);

	return false_;
}


/* Utility to jump spaces and cr/lf */
static cJSON_Parser *	buffer_skip_spaces (cJSON_Parser * buffer)
{
	if (!buffer || !buffer->content)
		return NULL;

	if (cannot_access_at_index(buffer, 0))
		return buffer;

	while (can_access_at_index(buffer, 0) && (buffer_at_offset(buffer)[0] <= 32))
		buffer->offset++;

	if (buffer->offset == buffer->length)
		buffer->offset--;

	return buffer;
}

/*
 * cJSON_ParseObject - When the next item is an Object, (recursively) parse/build it out
 *
 * Arguments:
 *	item 		- An item that we're building
 *	input_buffer	- The context of the JSON we're parsing
 *
 * Return Value:
 *	true_		- An array (list) was successfully parsed
 *	false_		- The array was not successfully parsed
 *			  You must terminate processing.
 */
static cJSON_bool	cJSON_ParseObject (cJSON *item, cJSON_Parser *input_buffer)
{
	cJSON *		head = NULL; /* linked list head */
	cJSON *		current_item = NULL;

	if (input_buffer->depth++ >= CJSON_NESTING_LIMIT)
		return false_; 

	if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != '{'))
		goto fail; 

	input_buffer->offset++;
	buffer_skip_spaces(input_buffer);
	if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '}'))
		goto success; 

	/* check if we skipped to the end of the buffer */
	if (cannot_access_at_index(input_buffer, 0))
	{
		input_buffer->offset--;
		goto fail;
	}

	/* step back to character in front of the first element */
	input_buffer->offset--;

	/* loop through the comma separated array elements */
	do
	{
		/* allocate next item */
		cJSON *	new_item;

		if (!(new_item = cJSON_NewItem()))
			goto fail; /* allocation failure */

		/* attach next item to list */
		if (!head)
		{
			/* start the linked list */
			current_item = head = new_item;
		}
		else
		{
			/* add to the end and advance */
			current_item->next = new_item;
			new_item->prev = current_item;
			current_item = new_item;
		}

		/* nothing comes after the comma */
		/* XXX - We need to tolerate nothing coming after a final comma */
		if (cannot_access_at_index(input_buffer, 1))
			goto fail; 

		/* First, get the name of the name:value pair */
		input_buffer->offset++;
		buffer_skip_spaces(input_buffer);
		if (!cJSON_ParseString(current_item, input_buffer))
			goto fail; /* failed to parse name */
		buffer_skip_spaces(input_buffer);

		/*
		 * HACK - XXX - 
		 * cJSON_ParseString blindly puts the string it sees in _->valuestring.
		 * The first string we parse here is actually the _name_, not the _value_.
		 * So we just move it over and go on our way.
		 * It is possible to do something clearer.
		 */
		current_item->name = current_item->valuestring;
		current_item->valuestring = NULL;

		if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != ':'))
			goto fail; /* invalid object */

		/* Second, get the value of the name:value pair */
		input_buffer->offset++;
		buffer_skip_spaces(input_buffer);
		if (!cJSON_ParseValue(current_item, input_buffer))
			goto fail; /* failed to parse value */
		buffer_skip_spaces(input_buffer);
	}
	while (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ','));

	if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != '}'))
		goto fail; /* expected end of object */

	success:
	input_buffer->depth--;

	if (head != NULL)
		head->prev = current_item;

	item->type = cJSON_Object;
	item->child = head;

	input_buffer->offset++;
	return true_;

fail:
	if (head != NULL)
		cJSON_DeleteItem(&head);

	return false_;
}

/*
 * cJSON_ParseArray - When the next item is an array, (recursively) parse/build it out
 *
 * Arguments:
 *	item 		- An item that we're building
 *	input_buffer	- The context of the JSON we're parsing
 *
 * Return Value:
 *	true_		- An array (list) was successfully parsed
 *	false_		- The array was not successfully parsed
 *			  You must terminate processing.
 */
static cJSON_bool 	cJSON_ParseArray (cJSON *item, cJSON_Parser *input_buffer)
{
	cJSON *		head = NULL; 			/* head of the linked list */
	cJSON *		current_item = NULL;

	if (input_buffer->depth++ >= CJSON_NESTING_LIMIT)
		return false_;

	/* not an array */
	if (buffer_at_offset(input_buffer)[0] != '[')
		goto fail;

	input_buffer->offset++;
	buffer_skip_spaces(input_buffer);

	/* empty array */
	if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ']'))
		goto success;

	/* check if we skipped to the end of the buffer */
	if (cannot_access_at_index(input_buffer, 0))
	{
		input_buffer->offset--;
		goto fail;
	}

	/* step back to character in front of the first element */
	input_buffer->offset--;

	/* loop through the comma separated array elements */
	do
	{
		/* allocate next item */
		cJSON *	new_item;

		if (!(new_item = cJSON_NewItem()))
			goto fail; /* allocation failure */

		/* attach next item to list */
		if (!head)
		{
			/* start the linked list */
			current_item = head = new_item;
		}
		else
		{
			/* add to the end and advance */
			current_item->next = new_item;
			new_item->prev = current_item;
			current_item = new_item;
		}

		/* parse next value */
		input_buffer->offset++;
		buffer_skip_spaces(input_buffer);

		/* failed to parse value */
		if (!cJSON_ParseValue(current_item, input_buffer))
			goto fail; 
		buffer_skip_spaces(input_buffer);
	}
	while (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ','));

	if (cannot_access_at_index(input_buffer, 0) || buffer_at_offset(input_buffer)[0] != ']')
		goto fail; /* expected end of array */

success:
	input_buffer->depth--;

	if (head)
		head->prev = current_item;

	item->type = cJSON_Array;
	item->child = head;

	input_buffer->offset++;

	return true_;

fail:
	if (head)
		cJSON_DeleteItem(&head);

	return false_;
}

/*
 * cJSON_ParseValue	- Load the next Value into an Item
 *
 * Parameters:
 *	item		- The item that we are building
 *			  This should be a brand new cJSON_NewItem().
 *	input_buffer	- The JSON context we're building 'item' from.
 *			  'buffer_at_offset' is the next thing we're looking at
 *
 * Return value:
 *	true_		- A value was successfully parsed
 *	false_		- Nothing, or nonsense was found
 *			  You must terminate processing.
 * 
 * Notes:
 *	We fill in the 'type' and the name/valuestring of 'item'
 *
 *	Remember, json Values have 6 basic types:
 *	1. null
 *	2. true or false
 *	3. A Number
 *	4. A "String"
 *	5. An Object - { Name-value pairs }
 *	6. An Array - [ ordered sequence of values ]
 *
 *	Compound types (Object, Array) must recursively parse the buffer.
 */
static cJSON_bool	cJSON_ParseValue (cJSON *item, cJSON_Parser *input_buffer)
{
	/* Nothing in, nothing out */
	if (!input_buffer || !input_buffer->content)
		return false_; 

	/*
	 * The standard says (3) 
	 * "These literal names MUST be lowercase.  
	 *  No other literal names are allowed"
	 * Oh well...
	 */
	/* 1. Null */
	if (can_read(input_buffer, 4) && (strncmp(buffer_at_offset(input_buffer), "null", 4) == 0))
	{
		item->type = cJSON_NULL;
		input_buffer->offset += 4;
		return true_;
	}

	/* 2a. True */
	if (can_read(input_buffer, 4) && (strncmp(buffer_at_offset(input_buffer), "true", 4) == 0))
	{
		item->type = cJSON_True;
		input_buffer->offset += 4;
		return true_;
	}
	/* 2b. False */
	if (can_read(input_buffer, 5) && (strncmp(buffer_at_offset(input_buffer), "false", 5) == 0))
	{
		item->type = cJSON_False;
		input_buffer->offset += 5;
		return true_;
	}

	/* 3. Number */
	if (can_access_at_index(input_buffer, 0) && strchr("-0123456789", (int)buffer_at_offset(input_buffer)[0]))
		return cJSON_ParseNumber(item, input_buffer);

	/* 4. String */
	if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '\"'))
		return cJSON_ParseString(item, input_buffer);

	/* 5. Object */
	if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '{'))
		return cJSON_ParseObject(item, input_buffer);

	/* 6. Array */
	if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '['))
		return cJSON_ParseArray(item, input_buffer);

	return false_;
}


/* * * * * */
/*
 * cJSON_Parse - Deserialize a JSON object 
 *
 * Arguments:
 *	buffer_		- (INPUT) A JSON object, either as a C string or as a buffer of bytes
 *	buffer_length	- (INPUT) The maximum number of bytes in 'buffer_' to process
 *			  - If 0, then 'buffer_' must be a nul-terminated C string.
 *			  - If > 0, then 'buffer_' does not need to be nul terminated!
 *	consumed	- (OUTPUT) How many bytes of 'buffer' were actually processed
 *			  - On success - This will always be <= buffer_length or wherever 
 *					 a nul is found, so that buffer_[consumed] is a 
 *					 valid pointer.
 *			  - On failure - This will be set to the byte where the error occurred.
 *
 * Return value:
 *	Non-NULL	- A cJSON object containing whatever was parsed.
 *			  - A dict (object), an array (list), a string, a number, or a boolean
 *	NULL		- Some sort of error occurred.
 *			  - buffer_ was NULL
 *			  - A new cJSON object could not be created (out of memory)
 *			  - buffer_ contained something that could not be parsed
 *			  - We did not complete an object within 'buffer_length' bytes.
 *			- Call cJSON_getErrorPtr() for more information.
 */
cJSON * cJSON_Parse (const char *buffer_, size_t buffer_length, size_t *consumed)
{
	cJSON_Parser	buffer = { 0, 0, 0, 0 };
	cJSON *		item = NULL;

	if (!consumed || !buffer_)
		return NULL;

	if (buffer_length == 0)
		buffer_length = strlen(buffer_) + 1;

	buffer.content = buffer_;
	buffer.length = buffer_length;
	buffer.offset = 0;

	if (!(item = cJSON_NewItem()))
		goto fail;

	if (!cJSON_ParseValue(item, buffer_skip_spaces(&buffer)))
		goto fail;

	buffer_skip_spaces(&buffer);

	*consumed = buffer.offset;
	return item;

fail:
	if (item)
		cJSON_DeleteItem(&item);

	*consumed = buffer.offset;
	return NULL;
}

/*********************************************************************************/
/* Serialize a JSON tree into a string */
typedef struct
{
	char *		buffer;
	size_t 		length;
	size_t 		offset;
	size_t 		depth; /* current nesting depth (for formatted generation) */
	cJSON_bool 	noalloc;
	cJSON_bool 	compact;	/* One line, or pretty printed? */
} cJSON_Generator;

static cJSON_bool	cJSON_GenerateValue		(const cJSON * item, cJSON_Generator *output_buffer);
static cJSON_bool	cJSON_GenerateArray		(const cJSON * item, cJSON_Generator *output_buffer);
static cJSON_bool	cJSON_GenerateDict		(const cJSON * item, cJSON_Generator *output_buffer);
static cJSON_bool	cJSON_GenerateString_ptr 	(const char *input, cJSON_Generator *output_buffer);
static cJSON_bool	cJSON_GenerateNumber 		(const cJSON *item, cJSON_Generator *output_buffer);
static cJSON_bool	cJSON_GenerateString 		(const cJSON *item, cJSON_Generator *p);

/* realloc cJSON_Generator if necessary to have at least "needed" bytes more */
static char *	ensure (cJSON_Generator *p, size_t needed)
{
	size_t 		newsize = 0;

	if ((p == NULL) || (p->buffer == NULL))
		return NULL;

	/* make sure that offset is valid */
	if ((p->length > 0) && (p->offset >= p->length))
		return NULL;

	/* sizes bigger than INT_MAX are currently not supported */
	if (needed > INT_MAX)
		return NULL;

	needed += p->offset + 1;
	if (needed <= p->length)
		return p->buffer + p->offset;

	if (p->noalloc)
		return NULL;

	/* calculate new buffer size */
	if (needed > (INT_MAX / 2))
	{
		/* overflow of int, use INT_MAX if possible */
		if (needed <= INT_MAX)
			newsize = INT_MAX;
		else
			return NULL;
	}
	else
		newsize = needed * 2;

	RESIZE(p->buffer, char, newsize);
	p->length = newsize;

	return p->buffer + p->offset;
}

/* calculate the new length of the string in a cJSON_Generator and update the offset */
static void	update_offset (cJSON_Generator *buffer)
{
	const char *	buffer_pointer = NULL;

	if ((buffer == NULL) || (buffer->buffer == NULL))
		return;
	buffer_pointer = buffer->buffer + buffer->offset;

	buffer->offset += strlen((const char*)buffer_pointer);
}


/* Render an array to text */
static cJSON_bool	cJSON_GenerateArray (const cJSON *item, cJSON_Generator *output_buffer)
{
	char *	output_pointer = NULL;
	size_t 	length = 0;
	cJSON *	current_element = item->child;

	if (!output_buffer)
		return false_;

	/* Compose the output array. */
	/* opening square bracket */
	if (!(output_pointer = ensure(output_buffer, 1)))
		return false_;

	*output_pointer = '[';
	output_buffer->offset++;
	output_buffer->depth++;

	while (current_element)
	{
		if (!cJSON_GenerateValue(current_element, output_buffer))
			return false_;

		update_offset(output_buffer);

		if (current_element->next)
		{
			length = (size_t) (output_buffer->compact ? 1 : 2);
			if (!(output_pointer = ensure(output_buffer, length + 1)))
				return false_;
			*output_pointer++ = ',';
			if (!output_buffer->compact)
				*output_pointer++ = ' ';
			*output_pointer = '\0';
			output_buffer->offset += length;
		}

		current_element = current_element->next;
	}

	if (!(output_pointer = ensure(output_buffer, 2)))
		return false_;

	*output_pointer++ = ']';
	*output_pointer = '\0';
	output_buffer->depth--;

	return true_;
}

/* Render an object to text. */
static cJSON_bool	cJSON_GenerateDict (const cJSON *item, cJSON_Generator *output_buffer)
{
	char *	output_pointer = NULL;
	size_t 	length = 0;
	cJSON *	current_item = item->child;

	if (!output_buffer)
		return false_;

	/* Compose the output: */
	length = (output_buffer->compact ? 1 : 2); 
	if (!(output_pointer = ensure(output_buffer, length + 1)))
		return false_;

	*output_pointer++ = '{';
	output_buffer->depth++;
	if (!output_buffer->compact)
		*output_pointer++ = '\n';
	output_buffer->offset += length;

	while (current_item)
	{
		if (!output_buffer->compact)
		{
			size_t	i;

			if (!(output_pointer = ensure(output_buffer, output_buffer->depth)))
				return false_;

			for (i = 0; i < output_buffer->depth; i++)
				*output_pointer++ = '\t';
			output_buffer->offset += output_buffer->depth;
		}

		/* print name */
		if (!cJSON_GenerateString_ptr(current_item->name, output_buffer))
			return false_;
		update_offset(output_buffer);

		length = (size_t) (output_buffer->compact ? 1 : 2);
		if (!(output_pointer = ensure(output_buffer, length)))
			return false_;
		*output_pointer++ = ':';
		if (!output_buffer->compact)
			*output_pointer++ = '\t';
		output_buffer->offset += length;

		/* print value */
		if (!cJSON_GenerateValue(current_item, output_buffer))
			return false_;
		update_offset(output_buffer);

		/* print comma if not last */
		length = (output_buffer->compact ? 0 : 1) + (current_item->next ? 1 : 0);
		if (!(output_pointer = ensure(output_buffer, length + 1)))
			return false_;
		if (current_item->next)
			*output_pointer++ = ',';

		if (!output_buffer->compact)
			*output_pointer++ = '\n';
		*output_pointer = '\0';
		output_buffer->offset += length;

		current_item = current_item->next;
	}

	if (!(output_pointer = ensure(output_buffer, output_buffer->compact ? 2 : (output_buffer->depth + 1))))
		return false_;
	if (!output_buffer->compact)
	{
		size_t	i;
		for (i = 0; i < (output_buffer->depth - 1); i++)
			*output_pointer++ = '\t';
	}
	*output_pointer++ = '}';
	*output_pointer = '\0';
	output_buffer->depth--;

	return true_;
}


/* Render a value to text. */
static cJSON_bool	cJSON_GenerateValue (const cJSON *item, cJSON_Generator *output_buffer)
{
	char *output = NULL;

	if (!item || !output_buffer)
		return false_;

	switch (item->type)
	{
	    case cJSON_NULL:
		if (!(output = ensure(output_buffer, 5)))
			return false_;
		strlcpy(output, "null", 5);
		return true_;

	    case cJSON_False:
		if (!(output = ensure(output_buffer, 6)))
			return false_;
		strlcpy(output, "false", 6);
		return true_;

	    case cJSON_True:
		if (!(output = ensure(output_buffer, 5)))
			return false_;
		strlcpy(output, "true", 5);
		return true_;

	    case cJSON_Number:
		return cJSON_GenerateNumber(item, output_buffer);

	    case cJSON_String:
		return cJSON_GenerateString(item, output_buffer);

	    case cJSON_Array:
		return cJSON_GenerateArray(item, output_buffer);

	    case cJSON_Object:
		return cJSON_GenerateDict(item, output_buffer);

	    default:
		return false_;
	}
}


/* securely comparison of floating-point variables */
static cJSON_bool	compare_double (double a, double b)
{
	double	maxVal = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
	return (fabs(a - b) <= maxVal * DBL_EPSILON);
}

/* I do not currently have a use case for this */
#if 0
static	cJSON_bool	is_double_an_int (double a1)
{
	double	a2;

	a2 = round(a1);
	if (fabs(a2 - a1) < DBL_EPSILON)
		return true_;
	return false_;
}
#endif


/* Render the number nicely from the given item into a string. */
static cJSON_bool	cJSON_GenerateNumber (const cJSON *item, cJSON_Generator *output_buffer)
{
	char *		output_pointer 	= NULL;
	double 		d;
	int 		length 		= 0;
	size_t 		i 		= 0;
	char 		number_buffer[26] = {0}; /* temporary buffer to print the number into */
	char 		decimal_point 	= '.';
	double 		test 		= 0.0;

	if (!output_buffer || !item)
		return false_;
	d = item->valuedouble;

	/* This checks for NaN and Infinity */
	if (isnan(d) || isinf(d))
		length = snprintf(number_buffer, sizeof(number_buffer), "null");
	else
	{
		/* Try 15 decimal places of precision to avoid nonsignificant nonzero digits */
		length = snprintf(number_buffer, sizeof(number_buffer), "%1.15g", d);

		/* Check whether the original double can be recovered */
		/* If not, print with 17 decimal places of precision */
		if ((sscanf(number_buffer, "%lg", &test) != 1) || !compare_double((double)test, d))
			length = snprintf(number_buffer, sizeof(number_buffer), "%1.17g", d);
	}

	/* snprintf failed or buffer overrun occurred */
	if ((length < 0) || (length > (int)(sizeof(number_buffer) - 1)))
		return false_;

	/* reserve appropriate space in the output */
	if (!(output_pointer = ensure(output_buffer, (size_t)length + sizeof(""))))
		return false_;

	/* copy the printed number to the output and replace locale
	 * dependent decimal point with '.' */
	for (i = 0; i < ((size_t)length); i++)
	{
		if (number_buffer[i] == decimal_point)
		{
			output_pointer[i] = '.';
			continue;
		}

		output_pointer[i] = number_buffer[i];
	}
	output_pointer[i] = '\0';

	output_buffer->offset += (size_t)length;

	return true_;
}


/* Render the cstring provided to an escaped version that can be printed. */
static cJSON_bool	cJSON_GenerateString_ptr (const char *input, cJSON_Generator *output_buffer)
{
	const char *	input_pointer = NULL;
	char *		output = NULL;
	char *		output_pointer = NULL;
	size_t 		output_length = 0;
	/* numbers of additional characters needed for escaping */
	size_t		escape_characters = 0;

	if (output_buffer == NULL)
		return false_;

	/* empty string */
	if (!input)
	{
		if (!(output = ensure(output_buffer, sizeof("\"\""))))
			return false_;
		strlcpy(output, "\"\"", 3);

		return true_;
	}

	/* set "flag" to 1 if something needs to be escaped */
	for (input_pointer = input; *input_pointer; input_pointer++)
	{
		switch (*input_pointer)
		{
		    case '\"':
		    case '\\':
		    case '\b':
		    case '\f':
		    case '\n':
		    case '\r':
		    case '\t':
			/* one character escape sequence */
			escape_characters++;
			break;
		    default:
			if (*input_pointer < 32)
			{
				/* UTF-16 escape sequence uXXXX */
				escape_characters += 5;
			}
			break;
		}
	}
	output_length = (size_t)(input_pointer - input) + escape_characters;

	if (!(output = ensure(output_buffer, output_length + sizeof("\"\""))))
		return false_;

	/* no characters have to be escaped */
	if (escape_characters == 0)
	{
		output[0] = '\"';
		memcpy(output + 1, input, output_length);
		output[output_length + 1] = '\"';
		output[output_length + 2] = '\0';

		return true_;
	}

	output[0] = '\"';
	output_pointer = output + 1;
	/* copy the string */
	for (input_pointer = input; *input_pointer; input_pointer++, output_pointer++)
	{
		/* normal character, copy */
		if ((*input_pointer > 31) && (*input_pointer != '\"') && (*input_pointer != '\\'))
			*output_pointer = *input_pointer;
		else
		{
			/* character needs to be escaped */
			*output_pointer++ = '\\';
			switch (*input_pointer)
			{
			    case '\\':
				*output_pointer = '\\';
				break;
			    case '\"':
				*output_pointer = '\"';
				break;
			    case '\b':
				*output_pointer = 'b';
				break;
			    case '\f':
				*output_pointer = 'f';
				break;
			    case '\n':
				*output_pointer = 'n';
				break;
			    case '\r':
				*output_pointer = 'r';
				break;
			    case '\t':
				*output_pointer = 't';
				break;
			    default:
				/* escape and print as unicode codepoint */
				/* XXX - Is 6 verified to be correct here? */
				snprintf(output_pointer, 6, "u%04x", *input_pointer);
				output_pointer += 4;
				break;
			}
		}
	}
	output[output_length + 1] = '\"';
	output[output_length + 2] = '\0';

	return true_;
}

/* Invoke print_string_ptr (which is useful) on an item. */
static cJSON_bool	cJSON_GenerateString (const cJSON *item, cJSON_Generator * p)
{
	return cJSON_GenerateString_ptr(item->valuestring, p);
}

char *	cJSON_Generate (const cJSON * item, cJSON_bool compact)
{
	size_t 			default_buffer_size = 256;
	cJSON_Generator 	buffer[1];		/* What the? */

	memset(buffer, 0, sizeof(buffer));

	/* create buffer */
	if (!(buffer->buffer = (char *) new_malloc(default_buffer_size)))
		goto fail;
	buffer->length = default_buffer_size;
	buffer->compact = compact;

	/* print the value */
	if (!cJSON_GenerateValue(item, buffer))
		goto fail;
	update_offset(buffer);

	return buffer->buffer;

fail:
	if (buffer->buffer != NULL)
		new_free(&(buffer->buffer));

	return NULL;
}


/******************************************************************************************/
/*
 * cJSON_NewItem -- Begin the life of a JSON node
 * 
 * Notes:
 *	This is internal only.
 *	Normal users will call things like cJSON_Create_Number() 
 */
static cJSON *	cJSON_NewItem (void)
{
	cJSON * node;

	if ((node = (cJSON *)new_malloc(sizeof(cJSON))))
		memset(node, 0, sizeof(cJSON));

	return node;
}

/*
 * cJSON_DeleteItem -- End the life of a JSON node -- recursively!
 *
 * Parameters:
 *	item 	- A value previously returned by a cJSON_NewItem() function
 *		  It's ok if you did stuff to it.
 *
 * Notes:
 *	You must not attempt to reference 'item' after calling this function.
 *	Ideally, 'item' should be a double ptr, so we can NULL it.
 *	(ie, treat it like new_free())
 */
void	cJSON_DeleteItem (cJSON **item_)
{
	cJSON *next;
	cJSON *item;

	if (!item_ || !*item_)
		return;

	for (item = *item_, next = NULL; item; item = next)
	{
		next = item->next;

		if (item->child)
			cJSON_DeleteItem(&(item->child));

		if (item->valuestring)
			new_free(&(item->valuestring));

		if (item->name)
			new_free(&(item->name));

		new_free(&item);
	}
	*item_ = NULL;
}


char *	cJSON_GetStringValue (const cJSON *item)
{
	if (!cJSON_IsString(item))
		return NULL;

	return item->valuestring;
}

double	cJSON_GetNumberValue (const cJSON *item)
{
	if (!cJSON_IsNumber(item))
		return (double) NAN;

	return item->valuedouble;
}

/* Get Array size/item / object item. */
int	cJSON_GetArraySize (const cJSON *array)
{
	cJSON *	child = NULL;
	size_t	size = 0;

	if (!array)
		return 0;

	for (child = array->child; child; child = child->next)
		size++;

	/* FIXME: Can overflow here. Cannot be fixed without breaking the API */
	return (int)size;
}

static cJSON *	get_array_item (const cJSON *array, size_t index)
{
	cJSON *current_child = NULL;

	if (array == NULL)
		return NULL;

	for (current_child = array->child; 
	     current_child && index > 0; 
	     current_child = current_child->next)
		index--;

	return current_child;
}

cJSON * cJSON_GetArrayItem (const cJSON *array, int index)
{
	if (index < 0)
		return NULL;

	return get_array_item(array, (size_t)index);
}

static cJSON *	get_object_item (const cJSON * object, const char * name, const cJSON_bool case_sensitive)
{
	cJSON *	current_element = NULL;

	if (!object || !name)
		return NULL;

	current_element = object->child;
	if (case_sensitive)
	{
		while (current_element && current_element->name && (strcmp(name, current_element->name)))
			current_element = current_element->next;
	}
	else
	{
		while (current_element && current_element->name && my_stricmp(name, current_element->name))
			current_element = current_element->next;
	}

	if (!current_element || !current_element->name)
		return NULL;

	return current_element;
}

cJSON * cJSON_GetObjectItem (const cJSON * object, const char * string)
{
	return get_object_item(object, string, false_);
}

cJSON * cJSON_GetObjectItemCaseSensitive (const cJSON * object, const char * string)
{
	return get_object_item(object, string, true_);
}

cJSON_bool	cJSON_HasObjectItem (const cJSON *object, const char *string)
{
	return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

/* Utility for array list handling. */
static void	suffix_object (cJSON *prev, cJSON *item)
{
	prev->next = item;
	item->prev = prev;
}

cJSON_bool	cJSON_AddItemToArray (cJSON *array, cJSON *item)
{
	cJSON *child = NULL;

	if (!item || !array || array == item)
		return false_;

	/*
	 * To find the last item in array quickly, we use prev in array
	 */
	if (!(child = array->child))
	{
		/* list is empty, start new one */
		array->child = item;
		item->prev = item;
		item->next = NULL;
	}
	else
	{
		/* append to the end */
		if (child->prev)
		{
			suffix_object(child->prev, item);
			array->child->prev = item;
		}
	}

	return true_;
}

/*
 * XXX This should verify if 'name' exists in 'object' already.
 */
cJSON_bool	cJSON_AddItemToObject (cJSON *object, const char *name, cJSON *item)
{
	if (!object || !name || !item || object == item)
		return false_;

	malloc_strcpy(&item->name, name);
	return cJSON_AddItemToArray(object, item);
}

#define cJSON_AddTypeToObject(type) 								\
cJSON *	cJSON_Add ## type ## ToObject (cJSON *object, const char *name) 			\
{												\
	cJSON *var = cJSON_Create ## type ();							\
												\
	if (cJSON_AddItemToObject(object, name, var))						\
		return var;									\
	else											\
	{											\
		cJSON_DeleteItem(&var);								\
		return NULL;									\
	}											\
}
cJSON_AddTypeToObject(Null)
cJSON_AddTypeToObject(True)
cJSON_AddTypeToObject(False)
cJSON_AddTypeToObject(Object)
cJSON_AddTypeToObject(Array)


#define cJSON_AddTypeToObjectWithInitialValue(ctype, jsontype)					\
cJSON *	cJSON_Add ## jsontype ## ToObject (cJSON *object, const char *name, const ctype initial) \
{												\
	cJSON *x = cJSON_Create ## jsontype ( initial );					\
												\
	if (cJSON_AddItemToObject(object, name, x))						\
		return x ;									\
	else											\
	{											\
		cJSON_DeleteItem(&x);								\
		return NULL;									\
	}											\
}
cJSON_AddTypeToObjectWithInitialValue(cJSON_bool, Bool)
cJSON_AddTypeToObjectWithInitialValue(double, Number)
cJSON_AddTypeToObjectWithInitialValue(char *, String)


/************/
cJSON *	cJSON_DetachItemViaPointer (cJSON *parent, cJSON *item)
{
	if ((parent == NULL) || (item == NULL) || (item != parent->child && item->prev == NULL))
		return NULL;

	if (item != parent->child)
		/* not the first element */
		item->prev->next = item->next;
	if (item->next != NULL)
		/* not the last element */
		item->next->prev = item->prev;

	if (item == parent->child)
		/* first element */
		parent->child = item->next;
	else if (item->next == NULL)
		/* last element */
		parent->child->prev = item->prev;

	/* make sure the detached item doesn't point anywhere anymore */
	item->prev = NULL;
	item->next = NULL;

	return item;
}

cJSON * cJSON_DetachItemFromArray (cJSON *array, int which)
{
	if (!array || which < 0)
		return NULL;

	return cJSON_DetachItemViaPointer(array, get_array_item(array, (size_t)which));
}

void	cJSON_DeleteItemFromArray (cJSON *array, int which)
{
	cJSON *	x;

	if (!array)
		return;

	if ((x = cJSON_DetachItemFromArray(array, which)))
		cJSON_DeleteItem(&x);
}

cJSON * cJSON_DetachItemFromObject (cJSON *object, const char *string)
{
	cJSON *	to_detach;

	if (!object || !string)
		return NULL;

	if ((to_detach = cJSON_GetObjectItem(object, string)))
		return cJSON_DetachItemViaPointer(object, to_detach);
	return NULL;
}

cJSON * cJSON_DetachItemFromObjectCaseSensitive (cJSON *object, const char *string)
{
	cJSON *	to_detach;

	if (!object || !string)
		return NULL;

	if ((to_detach = cJSON_GetObjectItemCaseSensitive(object, string)))
		return cJSON_DetachItemViaPointer(object, to_detach);
	return NULL;
}

void	cJSON_DeleteItemFromObject (cJSON *object, const char *string)
{
	cJSON *	x;

	if (!object || !string)
		return;

	if ((x = cJSON_DetachItemFromObject(object, string)))
		cJSON_DeleteItem(&x);
}

void	cJSON_DeleteItemFromObjectCaseSensitive (cJSON *object, const char *string)
{
	cJSON *	x;

	if (!object || !string)
		return;

	if ((x = cJSON_DetachItemFromObjectCaseSensitive(object, string)))
		cJSON_DeleteItem(&x);
}

/* Replace array/object items with new ones. */
cJSON_bool	cJSON_InsertItemInArray (cJSON *array, int which, cJSON *newitem)
{
	cJSON *	after_inserted = NULL;

	if (!array || which < 0 || !newitem)
		return false_;

	if (!(after_inserted = get_array_item(array, (size_t)which)))
		return cJSON_AddItemToArray(array, newitem);

	if (after_inserted != array->child && after_inserted->prev == NULL)
		/* return false if after_inserted is a corrupted array item */
		return false_;

	newitem->next = after_inserted;
	newitem->prev = after_inserted->prev;
	after_inserted->prev = newitem;
	if (after_inserted == array->child)
		array->child = newitem;
	else
		newitem->prev->next = newitem;
	return true_;
}

cJSON_bool	cJSON_ReplaceItemViaPointer (cJSON *parent, cJSON *item, cJSON *replacement)
{
	if (!parent || !parent->child || !item || !replacement)
		return false_;

	if (replacement == item)
		return true_;

	replacement->next = item->next;
	replacement->prev = item->prev;

	if (replacement->next)
		replacement->next->prev = replacement;
	if (parent->child == item)
	{
		if (parent->child->prev == parent->child)
			replacement->prev = replacement;
		parent->child = replacement;
	}
	else
	{   
		/*
		 * To find the last item in array quickly, we use prev in array.
		 * We can't modify the last item's next pointer where this item was the parent's child
		 */
		if (replacement->prev)
			replacement->prev->next = replacement;
		if (!replacement->next)
			parent->child->prev = replacement;
	}

	item->next = NULL;
	item->prev = NULL;
	cJSON_DeleteItem(&item);

	return true_;
}

cJSON_bool	cJSON_ReplaceItemInArray (cJSON *array, int which, cJSON *newitem)
{
	if (!array || which < 0 || !newitem)
		return false_;

	return cJSON_ReplaceItemViaPointer(array, get_array_item(array, (size_t)which), newitem);
}

static cJSON_bool	replace_item_in_object (cJSON *object, const char *string, cJSON *replacement, cJSON_bool case_sensitive)
{
	if (!object || !string || !replacement)
		return false_;

	/* replace the name in the replacement */
	malloc_strcpy(&replacement->name, string);
	return cJSON_ReplaceItemViaPointer(object, get_object_item(object, string, case_sensitive), replacement);
}

cJSON_bool	cJSON_ReplaceItemInObject (cJSON *object, const char *string, cJSON *newitem)
{
	if (!object || !string || !newitem)
		return false_;

	return replace_item_in_object(object, string, newitem, false_);
}

cJSON_bool	cJSON_ReplaceItemInObjectCaseSensitive (cJSON *object, const char *string, cJSON *newitem)
{
	if (!object || !string || !newitem)
		return false_;

	return replace_item_in_object(object, string, newitem, true_);
}

/*******************************************************************************/
/* Create basic types: */
cJSON *	cJSON_CreateNull (void)
{
	cJSON *item;

	if ((item = cJSON_NewItem()))
		item->type = cJSON_NULL;

	return item;
}

cJSON * cJSON_CreateTrue (void)
{
	cJSON *item;

	if ((item = cJSON_NewItem()))
		item->type = cJSON_True;

	return item;
}

cJSON * cJSON_CreateFalse (void)
{
	cJSON *	item;

	if ((item = cJSON_NewItem()))
		item->type = cJSON_False;

	return item;
}

cJSON * cJSON_CreateBool (cJSON_bool boolean)
{
	cJSON *	item;

	if ((item = cJSON_NewItem()))
		item->type = boolean ? cJSON_True : cJSON_False;

	return item;
}

cJSON * cJSON_CreateNumber (double num)
{
	cJSON *	item;

	if ((item = cJSON_NewItem()))
	{
		item->type = cJSON_Number;
		item->valuedouble = num;
	}

	return item;
}

cJSON * cJSON_CreateString (const char *string)
{
	cJSON *	item;

	if ((item = cJSON_NewItem()))
	{
		item->type = cJSON_String;
		/* XXX What if 'string' is NULL? */
		item->valuestring = malloc_strdup(string);
	}

	return item;
}

cJSON * cJSON_CreateArray (void)
{
	cJSON *	item;

	if ((item = cJSON_NewItem()))
		item->type = cJSON_Array;

	return item;
}

cJSON * cJSON_CreateObject (void)
{
	cJSON *	item;

	if ((item = cJSON_NewItem()))
		item->type = cJSON_Object;

	return item;
}


#define cJSON_CreateArrayOfType(ctype, NameType, JsonType) \
cJSON * cJSON_Create ## NameType ## Array (const ctype *values, int count)	\
{									\
	size_t	i = 0;							\
	cJSON *	n = NULL;						\
	cJSON *	p = NULL;						\
	cJSON *	a = NULL;						\
									\
	if ((count < 0) || (values == NULL))				\
		return NULL;						\
									\
	a = cJSON_CreateArray();					\
									\
	for (i = 0; a && (i < (size_t)count); i++)			\
	{								\
		if (!(n = cJSON_Create ## JsonType (values[i])))	\
		{							\
			cJSON_DeleteItem(&a);				\
			return NULL;					\
		}							\
									\
		if (!i)							\
			a->child = n;					\
		else							\
			suffix_object(p, n);				\
									\
		p = n;							\
	}								\
									\
	if (a && a->child)						\
		a->child->prev = n;					\
									\
	return a;							\
}

cJSON_CreateArrayOfType(int, Int, Number)
cJSON_CreateArrayOfType(float, Float, Number)
cJSON_CreateArrayOfType(double, Double, Number)
cJSON_CreateArrayOfType(char *, String, String)


/* * * * */
cJSON * cJSON_Duplicate_rec (const cJSON *item, size_t depth, cJSON_bool recurse);

/* Duplication */
cJSON * cJSON_Duplicate (const cJSON *item, cJSON_bool recurse)
{
	return cJSON_Duplicate_rec(item, 0, recurse);
}

cJSON * cJSON_Duplicate_rec (const cJSON *item, size_t depth, cJSON_bool recurse)
{
	cJSON *	newitem 	= NULL;
	cJSON *	child 		= NULL;
	cJSON *	next 		= NULL;
	cJSON *	newchild 	= NULL;

	/* Bail on bad ptr */
	if (!item || depth > CJSON_CIRCULAR_LIMIT)
		goto fail;

	/* Create new item */
	if (!(newitem = cJSON_NewItem()))
		goto fail;

	/* Copy over all vars */
	newitem->type = item->type;
	newitem->valuedouble = item->valuedouble;

	if (item->valuestring)
		newitem->valuestring = malloc_strdup(item->valuestring);

	if (item->name)
	{
		if (!(newitem->name = malloc_strdup(item->name)))
			goto fail;
	}

	/* If non-recursive, then we're done! */
	if (!recurse)
		return newitem;

	/* Walk the ->next chain for the child. */
	for (child = item->child; child; child = child->next)
	{
		if (depth >= CJSON_CIRCULAR_LIMIT)
			goto fail;

		/* Duplicate (with recurse) each item in the ->next chain */
		if (!(newchild = cJSON_Duplicate_rec(child, depth+1, true_)))
			goto fail;

		if (next != NULL)
		{
			/* If newitem->child already set, then crosswire ->prev and ->next and move on */
			next->next = newchild;
			newchild->prev = next;
			next = newchild;
		}
		else
		{
			/* Set newitem->child and move to it */
			newitem->child = newchild;
			next = newchild;
		}
	}

	if (newitem && newitem->child)
		newitem->child->prev = newchild;

	return newitem;

fail:
	cJSON_DeleteItem(&newitem);
	return NULL;
}


#define cJSON_IsType(type_) \
	cJSON_bool	cJSON_Is ## type_ (const cJSON * item)	\
	{							\
		if (item == NULL)				\
			return false_;				\
								\
		return (item->type) == cJSON_ ## type_ ;	\
	}	

cJSON_IsType(Invalid)
cJSON_IsType(False)
cJSON_IsType(True)
cJSON_IsType(NULL)
cJSON_IsType(Number)
cJSON_IsType(String)
cJSON_IsType(Array)
cJSON_IsType(Object)

cJSON_bool	cJSON_IsBool (const cJSON * item) { return (cJSON_IsTrue(item) || cJSON_IsFalse(item)); }
cJSON_bool	cJSON_IsNull (const cJSON * item) { return cJSON_IsNULL(item); }


/*************************************/
cJSON_bool	cJSON_Compare (const cJSON *a, const cJSON *b, cJSON_bool case_sensitive)
{
	if ((a == NULL) || (b == NULL) || (a->type != b->type))
		return false_;

	/* check if type is valid */
	switch (a->type)
	{
		case cJSON_False:
		case cJSON_True:
		case cJSON_NULL:
		case cJSON_Number:
		case cJSON_String:
		case cJSON_Array:
		case cJSON_Object:
			break;

		default:
			return false_;
	}

	/* identical objects are equal */
	if (a == b)
		return true_;

	switch (a->type)
	{
		/* in these cases and equal type is enough */
		case cJSON_False:
		case cJSON_True:
		case cJSON_NULL:
			return true_;

		case cJSON_Number:
			if (compare_double(a->valuedouble, b->valuedouble))
				return true_;
			return false_;

		case cJSON_String:
			if ((a->valuestring == NULL) || (b->valuestring == NULL))
				return false_;
			if (strcmp(a->valuestring, b->valuestring) == 0)
				return true_;
			return false_;

		case cJSON_Array:
		{
			cJSON *a_element = a->child;
			cJSON *b_element = b->child;

			for (; (a_element != NULL) && (b_element != NULL);)
			{
				if (!cJSON_Compare(a_element, b_element, case_sensitive))
					return false_;

				a_element = a_element->next;
				b_element = b_element->next;
			}

			/* one of the arrays is longer than the other */
			if (a_element != b_element)
				return false_;
			return true_;
		}

		case cJSON_Object:
		{
			cJSON *a_element = NULL;
			cJSON *b_element = NULL;

			cJSON_ArrayForEach(a_element, a)
			{
				/* TODO This has O(n^2) runtime, which is horrible! */
				b_element = get_object_item(b, a_element->name, case_sensitive);
				if (b_element == NULL)
				return false_;

				if (!cJSON_Compare(a_element, b_element, case_sensitive))
				return false_;
			}

			/* 
			 * doing this twice, once on a and b to prevent true comparison if a subset of b
			 * TODO: Do this the proper way, this is just a fix for now 
			 */
			cJSON_ArrayForEach(b_element, b)
			{
				if (!(a_element = get_object_item(a, b_element->name, case_sensitive)))
					return false_;

				if (!cJSON_Compare(b_element, a_element, case_sensitive))
					return false_;
			}

			return true_;
		}

		default:
		return false_;
	}
}

/* The patch stuff was removed.  It was huge!  I hope I don't regret that! */

