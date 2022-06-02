#ifndef	___DLFCN_H
#define	___DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

struct dl_find_object
{
  __extension__ unsigned long long int dlfo_flags;
  void *dlfo_map_start;		/* Beginning of mapping containing address.  */
  void *dlfo_map_end;		/* End of mapping.  */
  struct link_map *dlfo_link_map;
  void *dlfo_eh_frame;		/* Exception handling data of the object.  */
  __extension__ unsigned long long int __dflo_reserved[7];
};

/* If ADDRESS is found in an object, fill in *RESULT and return 0.
   Otherwise, return -1.  */
int _dl_find_object (void *__address, struct dl_find_object *__result);

#ifdef __cplusplus
}
#endif

#endif
