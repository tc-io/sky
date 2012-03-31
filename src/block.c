/*
 * Copyright (c) 2012 Ben Johnson, http://skylandlabs.com
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>

#include "dbg.h"
#include "block.h"

//==============================================================================
//
// Functions
//
//==============================================================================

//======================================
// Path Sorting
//======================================

// Compares two paths and sorts them by object id.
int compare_paths(const void *_a, const void *_b)
{
    Path **a = (Path**)_a;
    Path **b = (Path**)_b;

    // Sort by object id.
    if((*a)->object_id > (*b)->object_id) {
        return 1;
    }
    else if((*a)->object_id < (*a)->object_id) {
        return -1;
    }
    else {
        return 0;
    }
}

// Sorts paths in a block.
//
// block - The block containing the paths.
void sort_paths(Block *block)
{
    qsort(block->paths, block->path_count, sizeof(Path*), compare_paths);
}


//======================================
// Lifecycle
//======================================

// Creates a reference to an in-memory block.
//
// object_file - The object file that this block belongs to.
// info        - The header information about block.
//
// Returns a new block if successful, otherwise returns null.
Block *Block_create(ObjectFile *object_file, BlockInfo *info)
{
    Block *block;
    
    block = malloc(sizeof(Block)); check_mem(block);

    block->object_file = object_file;
    block->info = info;

    block->paths = NULL;
    block->path_count = 0;

    return block;
    
error:
    Block_destroy(block);
    return NULL;
}

// Removes a block reference from memory.
//
// block - The block to free.
void Block_destroy(Block *block)
{
    if(block) {
        block->object_file = NULL;
        block->info = NULL;

        // Destroy paths.
        uint32_t i=0;
        for(i=0; i<block->path_count; i++) {
            Path_destroy(block->paths[i]);
        }
        
        if(block->paths) free(block->paths);
        block->paths = NULL;
        block->path_count = 0;

        free(block);
    }
}


//======================================
// Serialization
//======================================

// Serializes a block to a given file at the file's current offset.
//
// block - The block to serialize.
// file    - The file descriptor.
//
// Returns 0 if successful, otherwise returns -1.
int Block_serialize(Block *block, FILE *file)
{
    int rc;

    // Validate.
    check(block != NULL, "Block required");
    check(file != NULL, "File descriptor required");
    
    // Write path count.
    rc = fwrite(&block->path_count, sizeof(block->path_count), 1, file);
    check(rc == 1, "Unable to serialize block path count: %d", block->path_count);
    
    // Loop over paths and delegate serialization to each path.
    uint32_t i;
    for(i=0; i<block->path_count; i++) {
        rc = Path_serialize(block->paths[i], file);
        check(rc == 0, "Unable to serialize block path: %d", i);
    }
    
    return 0;

error:
    return -1;
}

// Deserializes a block from a given file at the file's current offset.
//
// block - The block to serialize.
// fd    - The file descriptor.
//
// Returns 0 if successful, otherwise returns -1.
int Block_deserialize(Block *block, FILE *file)
{
    // TODO: Read path count.
    // TODO: Loop over paths and delegate serialization to each path.

    return 0;
}



//======================================
// Event Management
//======================================

// Adds an event to an in-memory block. The event will automatically be inserted
// into an existing path if one exists with the same object id or a new path 
// will be created.
//
// block - The block to insert the event into.
// event - The event that is to be inserted.
//
// Returns 0 if successful, otherwise returns -1.
int Block_add_event(Block *block, Event *event)
{
    uint32_t i;
    int rc;
    
    // Validation.
    check(block != NULL, "Block required");
    check(event != NULL, "Event required");
    check(event->object_id != 0, "Event object id cannot be 0");

    // Find existing path by object id.
    Path *path = NULL;
    for(i=0; i<block->path_count; i++) {
        if(block->paths[i]->object_id == event->object_id) {
            path = block->paths[i];
            break;
        }
    }
    
    // If matching path could not be found then create one.
    if(path == NULL) {
        // Allocate space for path.
        block->path_count++;
        block->paths = realloc(block->paths, sizeof(Event*) * block->path_count);
        check_mem(block->paths);

        // Create and append path.
        path = Path_create(event->object_id); check_mem(path);
        block->paths[block->path_count-1] = path;

        // Sort paths.
        sort_paths(block);
    }
    
    // Add event to path.
    rc = Path_add_event(path, event);
    check(rc == 0, "Unable to add event to path");

    return 0;

error:
    return -1;
}

// Removes an event from a path in the block. If the event's path no longer has
// any events then it will automatically be removed too.
//
// block - The block that contains the event.
// event - The event that will be removed.
//
// Returns 0 if successful, otherwise returns -1.
int Block_remove_event(Block *block, Event *event)
{
    uint32_t i, j, index;
    int rc;
    
    // Validation.
    check(block != NULL, "Block required");
    check(event != NULL, "Event required");
    check(event->object_id != 0, "Event object id cannot be 0");

    // Find event in paths.
    Path *path;

    for(i=0; i<block->path_count; i++) {
        bool found = false;
        path = block->paths[i];
        
        // If object id matches then search path.
        if(path->object_id == event->object_id) {
            for(j=0; j<path->event_count; j++) {
                // If event is found then remove it.
                if(path->events[j] == event) {
                    rc = Path_remove_event(path, event);
                    check(rc == 0, "Unable to remove event from path");
                    found = true;
                    break;
                }
            }
        }
        
        // Stop searching if the event was found.
        if(found) {
            index = i;
            break;
        }
    }
    
    // If path has no events then remove path from block.
    if(path != NULL && path->event_count == 0) {
        // Shift paths over.
        for(i=index+1; i<block->path_count; i++) {
            block->paths[i-1] = block->paths[i];
        }

        // Reallocate memory.
        block->path_count--;
        block->paths = realloc(block->paths, sizeof(Event*) * block->path_count);
        check_mem(block->paths);
    }

    return 0;

error:
    return -1;
}
