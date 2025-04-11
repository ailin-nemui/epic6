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
	cJSON_False,
	cJSON_True,
	cJSON_NULL,
	cJSON_Number,
	cJSON_String,
	cJSON_Array,
	cJSON_Object
} cJSON_type;

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
} cJSON;

typedef enum cJSON_bool {
	false_ = 0,
	true_ = 1
} cJSON_bool;

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

	cJSON *		cJSON_Parse			(const char *, size_t, size_t *);
	char * 		cJSON_Generate			(const cJSON *, cJSON_bool);

	void		cJSON_DeleteItem		(cJSON **);

	int		cJSON_GetArraySize		(const cJSON *);
	cJSON *		cJSON_GetArrayItem		(const cJSON *, int);

	cJSON *		cJSON_GetObjectItem		(const cJSON *, const char *);
	cJSON *		cJSON_GetObjectItemCaseSensitive	(const cJSON *, const char *);
	cJSON_bool	cJSON_HasObjectItem		(const cJSON *, const char *);

	char *		cJSON_GetStringValue		(const cJSON *);
	double		cJSON_GetNumberValue		(const cJSON *);

	cJSON_bool	cJSON_IsInvalid			(const cJSON *);
	cJSON_bool	cJSON_IsFalse			(const cJSON *);
	cJSON_bool	cJSON_IsTrue			(const cJSON *);
	cJSON_bool	cJSON_IsBool			(const cJSON *);
	cJSON_bool	cJSON_IsNull			(const cJSON *);
	cJSON_bool	cJSON_IsNumber			(const cJSON *);
	cJSON_bool	cJSON_IsString			(const cJSON *);
	cJSON_bool	cJSON_IsArray			(const cJSON *);
	cJSON_bool	cJSON_IsObject			(const cJSON *);

	cJSON *		cJSON_CreateNull		(void);
	cJSON *		cJSON_CreateTrue		(void);
	cJSON *		cJSON_CreateFalse		(void);
	cJSON *		cJSON_CreateBool		(cJSON_bool);
	cJSON *		cJSON_CreateNumber		(double num);
	cJSON *		cJSON_CreateString		(const char *string);
	cJSON *		cJSON_CreateArray		(void);
	cJSON *		cJSON_CreateObject		(void);

	cJSON *		cJSON_CreateIntArray		(const int *, int);
	cJSON *		cJSON_CreateFloatArray		(const float *, int);
	cJSON *		cJSON_CreateDoubleArray		(const double *, int);
	cJSON *		cJSON_CreateStringArray		(const char **, int);

	cJSON_bool	cJSON_AddItemToArray		(cJSON *array, cJSON *item);
	cJSON_bool	cJSON_AddItemToObject		(cJSON *object, const char *name, cJSON *item);
	cJSON_bool	cJSON_AddItemToObjectCS		(cJSON *object, const char *name, cJSON *item);

	cJSON *		cJSON_DetachItemViaPointer	(cJSON *parent, cJSON * item);
	cJSON *		cJSON_DetachItemFromArray	(cJSON *array, int which);
	cJSON *		cJSON_DetachItemFromObject	(cJSON *object, const char *name);
	cJSON *		cJSON_DetachItemFromObjectCaseSensitive (cJSON *object, const char *name);
	void 		cJSON_DeleteItemFromArray	(cJSON *array, int which);
	void		cJSON_DeleteItemFromObject	(cJSON *object, const char *name);
	void		cJSON_DeleteItemFromObjectCaseSensitive (cJSON *object, const char *name);

	cJSON_bool	cJSON_InsertItemInArray		(cJSON *array, int which, cJSON *newitem); /* Shifts pre-existing items to the right. */
	cJSON_bool	cJSON_ReplaceItemViaPointer 	(cJSON *parent, cJSON * item, cJSON * replacement);
	cJSON_bool	cJSON_ReplaceItemInArray 	(cJSON *array, int which, cJSON *newitem);
	cJSON_bool	cJSON_ReplaceItemInObject 	(cJSON *object, const char *name, cJSON *newitem);
	cJSON_bool	cJSON_ReplaceItemInObjectCaseSensitive (cJSON *object, const char *name, cJSON *newitem);

	cJSON *		cJSON_Duplicate			(const cJSON *, cJSON_bool);
	cJSON_bool	cJSON_Compare			(const cJSON *, const cJSON *, const cJSON_bool);

	cJSON *		cJSON_AddNullToObject 		(cJSON *, const char *);
	cJSON *		cJSON_AddTrueToObject 		(cJSON *, const char *);
	cJSON *		cJSON_AddFalseToObject 		(cJSON *, const char *);
	cJSON *		cJSON_AddBoolToObject 		(cJSON *, const char *, const cJSON_bool);
	cJSON *		cJSON_AddNumberToObject	 	(cJSON *, const char *, const double);
	cJSON *		cJSON_AddStringToObject	 	(cJSON *, const char *, const char *);
	cJSON *		cJSON_AddObjectToDict 		(cJSON *, const char *);
	cJSON *		cJSON_AddArrayToObject 		(cJSON *, const char *);

	double		cJSON_SetNumberHelper		(cJSON *object, double);
	char *		cJSON_SetValuestring		(cJSON *object, const char *);

#define cJSON_SetIntValue(object, number) 	(( object ) ? ( object )->valueint = ( object )->valuedouble = ( number ) : ( number ))
#define cJSON_SetNumberValue(object, number) 	(( object ) ? cJSON_SetNumberHelper( object , (double) number ) : ( number ))

/* If the object is not a boolean type this does nothing and returns cJSON_Invalid else it returns the new type*/
#define cJSON_SetBoolValue(object, boolValue) ( 							\
    (object != NULL && ((object)->type & (cJSON_False|cJSON_True))) ? 					\
    (object)->type=((object)->type &(~(cJSON_False|cJSON_True)))|((boolValue)?cJSON_True:cJSON_False) : \
    cJSON_Invalid											\
)

/* Macro for iterating over an array or object */
#define cJSON_ArrayForEach(element, array) for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

#endif

