from collections import Counter
import re

print("-=[ Mem0ry Allocations Analyz3r ]=-")

trace_records = open("trace.txt", "rt").readlines()

def is_malloc(x):
    return x.find("malloc ") > 0

def is_free(x):
    return x.find("memory_free ") > 0

def get_len(x):
    return re.findall("len=(.*)\n", x)[0]

def get_buf(x):
    return re.findall("buf=(0x................)", x)[0]

def is_page_alloc(x):
    return x.find("memory_page_alloc ") > 0

def is_page_free(x):
    return x.find("memory_page_free ") > 0

def get_page(x):
    return re.findall("page=(0x................)", x)[0]

# buf -> [(size, is_freed), ...]
page_allocs = {}
mallocs = {}

def process_records():
    global page_allocs
    global mallocs
    global trace_records

    for l in trace_records:
        try:
            #
            # Maintain a hash dictionary that maps between buffer address and a list of
            # tuples, each describing the size of the buffer and whether it had been
            # freed.
            #
            if (is_malloc(l)):
                buf = get_buf(l)
                t = [get_len(l), False]
                if buf in mallocs:
                    if (mallocs[buf][-1][1] == False):
                        print("Buffer %s was already allocated." % buf)
                    mallocs[buf] += [t]
                else:
                    mallocs[buf] = [t]
            
            elif (is_free(l)):
                buf = get_buf(l)
                # check if buffer had been allocated
                if not buf in mallocs:
                    # print "Buffer %s never been allocated." % buf
                    pass
                else:
                    # check if buffer already freed 
                    if (mallocs[buf][-1][1] == True):
                        print("Buffer %s buf already been freed." % buf)
                    else:
                       mallocs[buf][-1][1] = True 

	    #
	    # TODO: It is possible to alloc_huge_page() and then free_page()
	    # it in 512 pieces, and vice versa. Add support for that.
	    #
            elif (is_page_alloc(l)):
                page = get_page(l)
                t = [4096, False]
                if page in page_allocs:
                    if (page_allocs[page][-1][1] == False):
                        print("Page %s was already allocated." % page)
                    page_allocs[page] += [t]
                else:
                    page_allocs[page] = [t]

            elif (is_page_free(l)):
                page = get_page(l)
                # check if page had been allocated
                if not page in page_allocs:
                    # print "Page %s never been allocated." % page
                    pass
                else:
                    # check if page already freed
                    if (page_allocs[page][-1][1] == True):
                        print("Page %s buf already been freed." % page)
                    else:
                       page_allocs[page][-1][1] = True

        except:
            print("Problem parsing line: '%s'" % l)
            raise


def process_counts():
    "Group number of allocations and deallocations by size"
    global mallocs

    sizes = {}
    total = 0

    print("Calculating size histograms...")

    for buf in mallocs:
        for desc in mallocs[buf]:
            size = desc[0]
            freed = desc[1]
            if (not size in sizes):
                sizes[size] = {'allocs':0, 'frees':0}

            sizes[size]['allocs'] += 1
            if (freed):
                sizes[size]['frees'] += 1

    sizes = sorted(sizes.items(), key=lambda k: (k[1]['allocs']-k[1]['frees']) * int(k[0]))

    # print buffers
    for sz in sizes:
        unfreed = (sz[1]['allocs'] - sz[1]['frees']) * int(sz[0])
        print(sz[0], sz[1], "Unallocated: %d bytes" % unfreed)
        total += unfreed 
        
    print("Total unfreed: %d bytes" % total)
    

if __name__ == "__main__":
    process_records()
    process_counts()    

