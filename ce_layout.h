#pragma once

#include "ce.h"

struct CeLayout_t;

typedef enum{
     CE_LAYOUT_TYPE_VIEW,
     CE_LAYOUT_TYPE_LIST,
     CE_LAYOUT_TYPE_TAB,
     CE_LAYOUT_TYPE_TAB_LIST,
}CeLayoutType_t;

// NOTE: We use double pointers so that the usage code can take pointers to layouts. I tried re-organizing these
//       structures with just single arrays, but when I resized those arrays with re-alloc, the user code's
//       pointers had the change to become invalid.

typedef struct{
     struct CeLayout_t** layouts;
     int64_t layout_count;
     bool vertical;
     CeRect_t rect;
}CeLayoutList_t;

typedef struct{
     struct CeLayout_t* root;
     struct CeLayout_t* current;
     CeRect_t rect;
}CeTabLayout_t;

typedef struct{
     struct CeLayout_t** tabs;
     struct CeLayout_t* current;
     int64_t tab_count;
     CeRect_t rect;
}CeTabListLayout_t;

typedef struct CeLayout_t{
     CeLayoutType_t type;

     union{
          CeView_t view;
          CeLayoutList_t list;
          CeTabLayout_t tab;
          CeTabListLayout_t tab_list;
     };
     bool popup;
}CeLayout_t;

typedef struct{
     CeLayout_t** layouts;
     int64_t layout_count;
}CeLayoutBufferInViewsResult_t;

CeLayout_t* ce_layout_tab_list_init(CeLayout_t* tab_layout);
CeLayout_t* ce_layout_tab_init(CeBuffer_t* buffer, CeRect_t rect);
CeLayout_t* ce_layout_tab_list_add(CeLayout_t* tab_list_layout);
int64_t ce_layout_tab_get_layout_count(CeLayout_t* layout);
void ce_layout_free(CeLayout_t** layout);
CeLayout_t* ce_layout_split(CeLayout_t* layout, bool vertical, bool always_add_last);
void ce_layout_distribute_rect(CeLayout_t* layout, CeRect_t rect);
bool ce_layout_resize_rect(CeLayout_t* root, CeLayout_t* layout, CeRect_t rect, CeDirection_t direction, bool expand, int64_t amount);
CeLayout_t* ce_layout_find_at(CeLayout_t* layout, CePoint_t point);
CeLayout_t* ce_layout_find_parent(CeLayout_t* root, CeLayout_t* node);
CeLayout_t* ce_layout_find_popup(CeLayout_t* root);
bool ce_layout_delete(CeLayout_t* root, CeLayout_t* node);
CeLayout_t* ce_layout_buffer_in_view(CeLayout_t* root, CeBuffer_t* buffer);
CeLayoutBufferInViewsResult_t ce_layout_buffer_in_views(CeLayout_t* root, CeBuffer_t* buffer);
