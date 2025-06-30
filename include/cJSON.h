/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

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

#ifndef cJSON__h
#define cJSON__h

/*
   Version 1.7.18+ (12c4bf1986c288950a3d06da757109a6aa1ece38)
   imported from github on 3/25/2025
 */

#include <stddef.h>

typedef enum cJSON_type {
	cJSON_Invalid = 0,
	cJSON_Bool,
	cJSON_NULL,
	cJSON_Number,
	cJSON_String,
	cJSON_Array,
	cJSON_Object
} cJSON_type;

typedef enum cJSON_bool {
	false_ = 0,
	true_ = 1
} cJSON_bool;

/* The cJSON structure: */
typedef struct cJSON
{
	/* The type of the item, interval value -- don't touch */
	cJSON_type	type;

	/* 
	 * When type in (array, object):
	 *   This points at the linked list of members of the container.
	 * When type not in (array, objet):
	 *   This is NULL
	 */
	struct cJSON *	child;

	/* 
	 * When this Item is a member of an (array, object):
	 *   This points at the neighbor members in that (array,object)
	 * When not a member of an (array, object):
	 *   This is NULL
	 */
	struct cJSON *	next;
	struct cJSON *	prev;

	/* 
	 * When this Item is a member of an (object):
	 *   This is the name ("string") of the name:value pair
	 * When not a member of an (object)
	 *   This is NULL
	 */
	char *		name;

	/* 
	 * When type in (String):
	 *   This is a UTF-8 C string.
	 *   Is it not in JSON representation format.
	 * When type is not (String):
	 *   This is NULL
	 */
	char *		valuestring;

	/*
	 * When type in (Number) 
	 *   This is an integer
	 * When type not in (Number)
	 *   This value is undefined / must not be used.
	 */
	double		valuedouble;

	cJSON_bool	valuebool;
} cJSON;

/* Limits how deeply nested arrays/objects can be before cJSON rejects to parse them.
 * This is to prevent stack overflows. */
#ifndef CJSON_NESTING_LIMIT
#define CJSON_NESTING_LIMIT 1000
#endif

/* Limits the length of circular references can be before cJSON rejects to parse them.
 * This is to prevent stack overflows. */
#ifndef CJSON_CIRCULAR_LIMIT
#define CJSON_CIRCULAR_LIMIT 10000
#endif

	/* Parse C string containing JSON to a JSON tree (recursively) */
	cJSON *		cJSON_Parse			(const char *, size_t, size_t *);

	/* Convert a JSON tree to a C string (recursively) */
	char * 		cJSON_Generate			(const cJSON *, cJSON_bool);

	/* Delete a JSON item */
	void		cJSON_DeleteItem		(cJSON **);

	/* Test a JSON item for its type */
	cJSON_bool	cJSON_IsInvalid			(const cJSON *);
	cJSON_bool	cJSON_IsFalse			(const cJSON *);
	cJSON_bool	cJSON_IsTrue			(const cJSON *);
	cJSON_bool	cJSON_IsBool			(const cJSON *);
	cJSON_bool	cJSON_IsNull			(const cJSON *);
	cJSON_bool	cJSON_IsNumber			(const cJSON *);
	cJSON_bool	cJSON_IsString			(const cJSON *);
	cJSON_bool	cJSON_IsArray			(const cJSON *);
	cJSON_bool	cJSON_IsObject			(const cJSON *);

	/* Create a JSON item without a value (you must provide) */
	cJSON *		cJSON_CreateNull		(void);
	cJSON *		cJSON_CreateTrue		(void);
	cJSON *		cJSON_CreateFalse		(void);
	cJSON *		cJSON_CreateBool		(cJSON_bool);
	cJSON *		cJSON_CreateNumber		(double num);
	cJSON *		cJSON_CreateString		(const char *string);
	cJSON *		cJSON_CreateArray		(void);
	cJSON *		cJSON_CreateObject		(void);

	/* * * */
	/* Create a JSON item with an array with N items of a certain type (but no values) */
	cJSON *		cJSON_CreateIntArray		(const int *, int);
	cJSON *		cJSON_CreateFloatArray		(const float *, int);
	cJSON *		cJSON_CreateDoubleArray		(const double *, int);
	cJSON *		cJSON_CreateStringArray		(const char **, int);

	/* Given a JSON Array, add an item to the end of it. */
	cJSON_bool	cJSON_AddItemToArray		(cJSON *array, cJSON *item);

	/* Given a JSON Array, make a new item the Nth item, shifting existing items to the right */
	cJSON_bool	cJSON_InsertItemInArray		(cJSON *array, int which, cJSON *newitem); 
	/* Given a JSON Array, replace the Nth item.... */
	cJSON_bool	cJSON_ReplaceItemInArray 	(cJSON *array, int which, cJSON *newitem);

	/* If this JSON item is an array, how big is it?  Get me the Nth item */
	int		cJSON_GetArraySize		(const cJSON *);
	cJSON *		cJSON_GetArrayItem		(const cJSON *, int);


	/* Remove and destroy the Nth item from an array */
	void 		cJSON_DeleteItemFromArray	(cJSON *array, int which);

	/* * * */
	/* Given a JSON object, add a key and value */
	cJSON_bool	cJSON_AddItemToObject		(cJSON *object, const char *name, cJSON *item);
	cJSON_bool	cJSON_AddItemToObjectCS		(cJSON *object, const char *name, cJSON *item);

	/* Given a JSON Object and a key, insert a new value into the object */
	cJSON *		cJSON_AddNullToObject 		(cJSON *, const char *);
	cJSON *		cJSON_AddTrueToObject 		(cJSON *, const char *);
	cJSON *		cJSON_AddFalseToObject 		(cJSON *, const char *);
	cJSON *		cJSON_AddBoolToObject 		(cJSON *, const char *, const cJSON_bool);
	cJSON *		cJSON_AddNumberToObject	 	(cJSON *, const char *, const double);
	cJSON *		cJSON_AddStringToObject	 	(cJSON *, const char *, const char *);
	cJSON *		cJSON_AddObjectToDict 		(cJSON *, const char *);
	cJSON *		cJSON_AddArrayToObject 		(cJSON *, const char *);

	/* Remove and return ("pop") the Nth item from an array */
	cJSON *		cJSON_DetachItemFromArray	(cJSON *array, int which);

	/* If this JSON item is an Object, get me this named item */
	cJSON *		cJSON_GetObjectItem		(const cJSON *, const char *);
	cJSON *		cJSON_GetObjectItemCaseSensitive	(const cJSON *, const char *);
	cJSON_bool	cJSON_HasObjectItem		(const cJSON *, const char *);

	/* Remove and return ("pop") a key from an object */
	cJSON *		cJSON_DetachItemFromObject	(cJSON *object, const char *name);
	cJSON *		cJSON_DetachItemFromObjectCaseSensitive (cJSON *object, const char *name);

	/* Given a JSON Object, replace the key with a new item */
	cJSON_bool	cJSON_ReplaceItemInObject 	(cJSON *object, const char *name, cJSON *newitem);
	cJSON_bool	cJSON_ReplaceItemInObjectCaseSensitive (cJSON *object, const char *name, cJSON *newitem);

	/* Remove and destroy a key from an object */
	void		cJSON_DeleteItemFromObject	(cJSON *object, const char *name);
	void		cJSON_DeleteItemFromObjectCaseSensitive (cJSON *object, const char *name);

	/* * * */
	/* If this JSON item is a string or a number, get me its C value */
	char *		cJSON_GetStringValue		(const cJSON *);
	double		cJSON_GetNumberValue		(const cJSON *);

	/* Given a JSON item, replace its _value_ */
	double		cJSON_SetNumberHelper		(cJSON *object, double);
	char *		cJSON_SetValuestring		(cJSON *object, const char *);

	/* * * */
	/* Given a JSON item, given it belongs to an array or object, remove it */
	cJSON *		cJSON_DetachItemViaPointer	(cJSON *parent, cJSON * item);

	/* Given a JSON Array or Object, given 'item' is a member, change 'item' with 'replacement'. */
	cJSON_bool	cJSON_ReplaceItemViaPointer 	(cJSON *parent, cJSON * item, cJSON * replacement);

	/* Duplicate a JSON item (recursively) */
	cJSON *		cJSON_Duplicate			(const cJSON *, cJSON_bool);

	/* Tell me, are these two items identical? (recursively) */
	cJSON_bool	cJSON_Compare			(const cJSON *, const cJSON *, const cJSON_bool);

/* Macro for iterating over an array or object */
#define cJSON_ArrayForEach(element, array) for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

#endif

