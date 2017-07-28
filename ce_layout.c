#include "ce_layout.h"

#include <stdlib.h>
#include <assert.h>

CeLayout_t* ce_layout_tab_list_init(CeLayout_t* tab_layout){
     CeLayout_t* tab_list_layout = calloc(1, sizeof(*tab_list_layout));
     if(!tab_list_layout) return NULL;
     tab_list_layout->type = CE_LAYOUT_TYPE_TAB_LIST;
     tab_list_layout->tab_list.tabs = malloc(sizeof(*tab_list_layout->tab_list.tabs));
     tab_list_layout->tab_list.tabs[0] = tab_layout;
     tab_list_layout->tab_list.tab_count = 1;
     tab_list_layout->tab_list.current = tab_layout;
     return tab_list_layout;
}

CeLayout_t* ce_layout_tab_init(CeBuffer_t* buffer){
     CeLayout_t* view_layout = calloc(1, sizeof(*view_layout));
     if(!view_layout) return NULL;
     view_layout->type = CE_LAYOUT_TYPE_VIEW;
     view_layout->view.buffer = buffer;

     CeLayout_t* tab_layout = calloc(1, sizeof(*tab_layout));
     if(!tab_layout) return NULL; // LEAK: leak view_layout
     tab_layout->type = CE_LAYOUT_TYPE_TAB;
     tab_layout->tab.root = view_layout;
     tab_layout->tab.current = view_layout;
     return tab_layout;
}

void ce_layout_free(CeLayout_t** root){
     CeLayout_t* layout = *root;
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               ce_layout_free(&layout->list.layouts[i]);
          }
          free(layout->list.layouts);
          layout->list.layout_count = 0;
          break;
     case CE_LAYOUT_TYPE_TAB:
          ce_layout_free(&layout->tab.root);
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
          for(int64_t i = 0; i < layout->tab_list.tab_count; i++){
               ce_layout_free(&layout->tab_list.tabs[i]);
          }
          break;
     }

     free(*root);
     *root = NULL;
}

static CeBuffer_t* ce_layout_find_buffer(CeLayout_t* layout){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          if(layout->view.buffer) return layout->view.buffer;
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               CeBuffer_t* found = ce_layout_find_buffer(layout->list.layouts[i]);
               if(found) return found;
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          return ce_layout_find_buffer(layout->tab.root);
     case CE_LAYOUT_TYPE_TAB_LIST:
          for(int64_t i = 0; i < layout->tab_list.tab_count; i++){
               CeBuffer_t* buffer = ce_layout_find_buffer(layout->tab_list.tabs[i]);
               if(buffer) return buffer;
          }
          break;
     }

     return NULL;
}

bool ce_layout_split(CeLayout_t* layout, bool vertical){
     CeLayout_t* parent_of_current = ce_layout_find_parent(layout, layout->tab.current);
     if(parent_of_current){
          CeBuffer_t* buffer = ce_layout_find_buffer(layout->tab.current);
          assert(buffer);
          switch(parent_of_current->type){
          default:
               break;
          case CE_LAYOUT_TYPE_LIST:
               if(parent_of_current->list.vertical == vertical){
                    CeLayout_t* new_layout = calloc(1, sizeof(*new_layout));
                    if(!new_layout) return false;
                    new_layout->type = CE_LAYOUT_TYPE_VIEW;
                    new_layout->view.buffer = buffer;

                    int64_t new_layout_count = parent_of_current->list.layout_count + 1;
                    parent_of_current->list.layouts = realloc(parent_of_current->list.layouts,
                                                              new_layout_count * sizeof(*parent_of_current->list.layouts));
                    if(parent_of_current->list.layouts) parent_of_current->list.layout_count = new_layout_count;
                    else return false;
                    parent_of_current->list.layouts[new_layout_count - 1] = new_layout;
               }else{
                    CeLayout_t* new_list_layout = calloc(1, sizeof(*new_list_layout));
                    if(!new_list_layout) return false;
                    new_list_layout->type = CE_LAYOUT_TYPE_LIST;
                    new_list_layout->list.layouts = malloc(sizeof(*new_list_layout->list.layouts));
                    if(!new_list_layout->list.layouts) return false;
                    new_list_layout->list.layout_count = 1;
                    new_list_layout->list.vertical = vertical;
                    new_list_layout->list.layouts[0] = layout->tab.current;

                    // point parent at new list
                    for(int64_t i = 0; i < parent_of_current->list.layout_count; i++){
                         if(parent_of_current->list.layouts[i] == layout->tab.current){
                              parent_of_current->list.layouts[i] = new_list_layout;
                              break;
                         }
                    }

                    return ce_layout_split(layout, vertical);
               }
               break;
          case CE_LAYOUT_TYPE_TAB:
          {
               CeLayout_t* list_layout = calloc(1, sizeof(*list_layout));
               list_layout->type = CE_LAYOUT_TYPE_LIST;
               list_layout->list.layout_count = 1;
               list_layout->list.layouts = calloc(list_layout->list.layout_count, sizeof(*list_layout->list.layouts));
               list_layout->list.layouts[0] = layout->tab.current;
               list_layout->list.vertical = vertical;
               parent_of_current->tab.root = list_layout;
               return ce_layout_split(layout, vertical);
          } break;
          case CE_LAYOUT_TYPE_TAB_LIST:
               if(layout->tab_list.current) return ce_layout_split(layout->tab_list.current, vertical);
               break;
          }

          return true;
     }

     return false;
}

void ce_layout_distribute_rect(CeLayout_t* layout, CeRect_t rect){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          layout->view.rect = rect;
          break;
     case CE_LAYOUT_TYPE_LIST:
     {
          layout->list.rect = rect;
          CeRect_t sliced_rect = rect;
          int64_t separator_lines = layout->list.layout_count - 1;

          if(layout->list.vertical){
               int64_t rect_height = rect.bottom - rect.top;
               int64_t slice_height = (rect_height - separator_lines) / layout->list.layout_count;
               int64_t leftover_lines = (rect_height - separator_lines) % layout->list.layout_count;
               sliced_rect.bottom = sliced_rect.top + slice_height;

               for(int64_t i = 0; i < layout->list.layout_count; i++){
                    if(leftover_lines > 0){
                         leftover_lines--;
                         sliced_rect.bottom++;
                    }

                    ce_layout_distribute_rect(layout->list.layouts[i], sliced_rect);

                    sliced_rect.top = sliced_rect.bottom + 1;
                    sliced_rect.bottom = sliced_rect.top + slice_height;
               }
          }else{
               int64_t rect_width = rect.right - rect.left;
               int64_t slice_width = (rect_width - separator_lines) / layout->list.layout_count;
               int64_t leftover_lines = (rect_width - separator_lines) % layout->list.layout_count;
               sliced_rect.right = sliced_rect.left + slice_width;

               for(int64_t i = 0; i < layout->list.layout_count; i++){
                    if(leftover_lines > 0){
                         leftover_lines--;
                         sliced_rect.right++;
                    }

                    ce_layout_distribute_rect(layout->list.layouts[i], sliced_rect);

                    sliced_rect.left = sliced_rect.right + 1;
                    sliced_rect.right = sliced_rect.left + slice_width;
               }
          }
     } break;
     case CE_LAYOUT_TYPE_TAB:
          layout->tab.rect = rect;
          ce_layout_distribute_rect(layout->tab.root, rect);
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
          layout->tab_list.rect = rect;
          rect.top++;
          for(int64_t i = 0; i < layout->tab_list.tab_count; i++){
               ce_layout_distribute_rect(layout->tab.root, rect);
          }
          break;
     }
}

CeLayout_t* ce_layout_find_at(CeLayout_t* layout, CePoint_t point){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          if(ce_point_in_rect(point, layout->view.rect)) return layout;
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               CeLayout_t* found = ce_layout_find_at(layout->list.layouts[i], point);
               if(found) return found;
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          return ce_layout_find_at(layout->tab.root, point);
     case CE_LAYOUT_TYPE_TAB_LIST:
          return ce_layout_find_at(layout->tab_list.current, point);
     }

     return NULL;
}

CeLayout_t* ce_layout_find_parent(CeLayout_t* root, CeLayout_t* node){
     switch(root->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < root->list.layout_count; i++){
               if(root->list.layouts[i] == node) return root;
               CeLayout_t* found = ce_layout_find_parent(root->list.layouts[i], node);
               if(found) return found;
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          if(root->tab.root == node) return root;
          else return ce_layout_find_parent(root->tab.root, node);
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
          for(int64_t i = 0; i < root->tab_list.tab_count; i++){
               if(root->tab_list.tabs[i] == node) return root;
               CeLayout_t* found = ce_layout_find_parent(root->tab_list.tabs[i], node);
               if(found) return found;
          }
          break;
     }

     return NULL;
}

bool ce_layout_delete(CeLayout_t* root, CeLayout_t* node){
     CeLayout_t* parent = ce_layout_find_parent(root, node);
     if(!parent) return false;

     switch(parent->type){
     default:
          return false;
     case CE_LAYOUT_TYPE_LIST:
     {
          // find index of matching node
          int64_t index = -1;
          for(int64_t i = 0; i < parent->list.layout_count; i++){
               if(parent->list.layouts[i] == node){
                    index = i;
                    break;
               }
          }

          if(index == -1) return false;

          ce_layout_free(&node);

          // remove element, keeping element order
          int64_t new_count = parent->list.layout_count - 1;
          for(int64_t i = index; i < new_count; i++){
               parent->list.layouts[i] = parent->list.layouts[i + 1];
          }
          parent->list.layout_count = new_count;

          if(new_count == 0) return ce_layout_delete(root, parent);
     } break;
     case CE_LAYOUT_TYPE_TAB:
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
     {
          // find index of matching node
          int64_t index = -1;
          for(int64_t i = 0; i < parent->tab_list.tab_count; i++){
               if(parent->tab_list.tabs[i] == node){
                    index = i;
                    break;
               }
          }

          if(index == -1) return false;

          ce_layout_free(&node);

          // remove element, keeping element order
          int64_t new_count = parent->tab_list.tab_count - 1;
          for(int64_t i = index; i < new_count; i++){
               parent->tab_list.tabs[i] = parent->tab_list.tabs[i + 1];
          }
          parent->tab_list.tab_count = new_count;
     } break;
     }

     return true;
}
