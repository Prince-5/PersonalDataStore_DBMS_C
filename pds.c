#include<stdio.h>
#include<string.h>
#include<errno.h>
#include<stdlib.h>

#include "bst.h"
#include "pds.h"

//Define the global variable
struct PDS_RepoInfo repo_handle;

int check_freelist(int offset)
{
    for(int i=0;i<MAX_FREE;i++)
    {
        if(repo_handle.free_list[i]==offset)
            return 1;
    }
    return 0;
}

void printPreorder(struct BST_Node* node) 
{ 
     if (node == NULL) 
          return; 

    //struct PDS_NdxInfo *ndx_entry = (struct PDS_NdxInfo *) (node->data); (Explicit Type Conversion)
  
     /* first write data of node */
     fwrite(node->data, sizeof(struct PDS_NdxInfo), 1, repo_handle.pds_ndx_fp);  

     //fseek(repo_handle.pds_ndx_fp, sizeof(struct PDS_NdxInfo),SEEK_CUR);

     /* then recur on left subtree */
     printPreorder(node->left_child);   
  
     /* now recur on right subtree */
     printPreorder(node->right_child); 
} 

int pds_create(char *repo_name);

int pds_open(char *repo_name, int rec_size)
{
    char repo_file[34];
    char ndx_file[34];

    if(repo_handle.repo_status == PDS_REPO_OPEN)
        return PDS_REPO_ALREADY_OPEN;
    
    strcpy(repo_handle.pds_name,repo_name);

    strcpy(repo_file,repo_name);
    strcat(repo_file,".dat");
    strcpy(ndx_file,repo_name);
    strcat(ndx_file,".ndx");

    repo_handle.pds_data_fp = (FILE *)fopen(repo_file,"rb+");
    if(repo_handle.pds_data_fp == NULL)
        perror(repo_file);

    repo_handle.pds_ndx_fp=(FILE *)fopen(ndx_file,"rb+");
    if(repo_handle.pds_ndx_fp == NULL)
        perror(ndx_file);

    repo_handle.repo_status = PDS_REPO_OPEN;
    repo_handle.rec_size = rec_size;
    repo_handle.pds_bst = NULL;
    
    fseek(repo_handle.pds_ndx_fp,0,SEEK_END);
    int end = ftell(repo_handle.pds_ndx_fp);
    
    if(end==0)
    {
        for(int i=0;i<MAX_FREE;i++)
        {
            repo_handle.free_list[i]=-1;
        }
    }
    else
    {
        fseek(repo_handle.pds_ndx_fp,0,SEEK_SET);
        fread(repo_handle.free_list,sizeof(int),MAX_FREE,repo_handle.pds_ndx_fp);
    }

    pds_load_ndx();
    fclose(repo_handle.pds_ndx_fp);
    return PDS_SUCCESS;
}

int pds_load_ndx()
{
    struct PDS_NdxInfo *index_Ndxrecord;
    while(!feof(repo_handle.pds_ndx_fp))
    {
        index_Ndxrecord=(struct PDS_NdxInfo*)malloc(sizeof(struct PDS_NdxInfo));
        fread(index_Ndxrecord,sizeof(struct PDS_NdxInfo),1,repo_handle.pds_ndx_fp);
        bst_add_node(&repo_handle.pds_bst,index_Ndxrecord->key,index_Ndxrecord);
    }
    return PDS_SUCCESS;
}

int put_rec_by_key(int key, void *rec)
{
    int offset,status,writesize;
    struct PDS_NdxInfo *ndx_entry;

    int i=0;
    while(i<MAX_FREE)
    {
        if(repo_handle.free_list[i]>-1)
        {
            offset=repo_handle.free_list[i];
	        repo_handle.free_list[i]=-1;
            fseek(repo_handle.pds_data_fp,offset,SEEK_SET);
            offset = ftell(repo_handle.pds_data_fp);
            break;
        }
        i++;
    }
    
    if(i==MAX_FREE)
    {
        //Go to end of the file.
        fseek(repo_handle.pds_data_fp,0,SEEK_END);
        offset = ftell(repo_handle.pds_data_fp);
    }

    fwrite(&key,sizeof(int),1,repo_handle.pds_data_fp);
    writesize = fwrite(rec,repo_handle.rec_size,1,repo_handle.pds_data_fp);	

    ndx_entry = (struct PDS_NdxInfo *)malloc(sizeof(struct PDS_NdxInfo *)); 
    ndx_entry->key=key;
    ndx_entry->offset=offset;

    status = bst_add_node(&repo_handle.pds_bst,key,ndx_entry);
    if(status != BST_SUCCESS)
    {
        fprintf(stderr,"unable to add index entry for key %d -Error %d\n",key,status);
        free(ndx_entry);
        //Remember: Data has already gone into the data file! Rollback by resetting the file pointer.
        fseek(repo_handle.pds_data_fp,offset,SEEK_SET); 
        status = PDS_ADD_FAILED;
    }
    else
        status = PDS_SUCCESS;
    
    return status;
}

int get_rec_by_ndx_key(int key, void *rec)
{
    struct PDS_NdxInfo *ndx_entry;
    struct BST_Node *bst_node;
    int offset,status,readsize;
    int *temp_key = (int *)malloc(sizeof(int));

    bst_node = bst_search(repo_handle.pds_bst,key);
    if(bst_node == NULL)
        status = PDS_REC_NOT_FOUND;
    else
    {
        ndx_entry = (struct PDS_NdxInfo*)(bst_node->data);
        offset = ndx_entry->offset;
        fseek(repo_handle.pds_data_fp,offset,SEEK_SET);
        fread(temp_key,sizeof(int),1,repo_handle.pds_data_fp);
        readsize = fread(rec,repo_handle.rec_size,1,repo_handle.pds_data_fp);
        status = PDS_SUCCESS;
    }
    return status;
}

int get_rec_by_non_ndx_key(void *key, void *rec, int (*matcher)(void *rec, void *key), int *io_count)
{
    //Go to the start of the data file.
    fseek(repo_handle.pds_data_fp,0,SEEK_SET);
    *io_count=0;
    int *temp_key = (int *)malloc(sizeof(int));
    
    int offset;

    while(!feof(repo_handle.pds_data_fp))
    {
        offset=ftell(repo_handle.pds_data_fp);
        if(check_freelist(offset)==0)
        {
            fread(temp_key,sizeof(int),1,repo_handle.pds_data_fp);
            fread(rec,repo_handle.rec_size,1,repo_handle.pds_data_fp);            
            (*io_count)++;
            int status = matcher(rec,key);
            if(status==0)
            {
                fseek(repo_handle.pds_data_fp,0,SEEK_END);
                return PDS_SUCCESS;
            }
        }
        else
        {
            fseek(repo_handle.pds_data_fp,(sizeof(int)+repo_handle.rec_size),SEEK_CUR);   
        }
    }
    return PDS_REC_NOT_FOUND;            
}

int pds_close()
{
    char ndx_file[34];
    strcpy(ndx_file,repo_handle.pds_name);
    strcat(ndx_file,".ndx");

    repo_handle.pds_ndx_fp=(FILE *)fopen(ndx_file,"wb");
    if(repo_handle.pds_ndx_fp == NULL)
        perror(ndx_file);

    fwrite(repo_handle.free_list,sizeof(int),MAX_FREE,repo_handle.pds_ndx_fp);
    printPreorder(repo_handle.pds_bst);
    
    strcpy(repo_handle.pds_name,"");
    fclose(repo_handle.pds_data_fp);
    fclose(repo_handle.pds_ndx_fp);
    bst_destroy(repo_handle.pds_bst);
    repo_handle.repo_status=PDS_REPO_CLOSED;

    return PDS_SUCCESS;
}

int update_by_key(int key, void *newrec)
{
    struct PDS_NdxInfo *ndx_entry;
    struct BST_Node *bst_node;
    int offset,status,writesize;

    bst_node = bst_search(repo_handle.pds_bst,key);
    if(bst_node == NULL)
        status = PDS_MODIFY_FAILED;
    else
    {
        ndx_entry = (struct PDS_NdxInfo*)(bst_node->data);
        offset = ndx_entry->offset;
        fseek(repo_handle.pds_data_fp,offset,SEEK_SET);
        fwrite(&key,sizeof(int),1,repo_handle.pds_data_fp);
        writesize = fwrite(newrec,repo_handle.rec_size,1,repo_handle.pds_data_fp);
        status = PDS_SUCCESS;
    }
    return status;
}

int delete_by_key(int key)
{
    struct PDS_NdxInfo *ndx_entry;
    struct BST_Node *bst_node;
    int offset;

    bst_node = bst_search(repo_handle.pds_bst,key);
    if(bst_node == NULL)
        return PDS_DELETE_FAILED;
    else
    {
        ndx_entry = (struct PDS_NdxInfo*)(bst_node->data);
        offset = ndx_entry->offset;
        int i=0;
        while(repo_handle.free_list[i]>-1 && i<MAX_FREE)
            i++;
        if(i==MAX_FREE) //if the free list is full, then delete operation should not work.
            return PDS_DELETE_FAILED;
        else
        {
            bst_del_node(&repo_handle.pds_bst,key);
            repo_handle.free_list[i]=offset;
            return PDS_SUCCESS;
        }    
    }
}

//Additional function undelete_by_offset added.
//This function performs a linear search in the freelist.
//If the offset is present in the freelist, it undeletes the record present at that offset in the data file.
int undelete_by_offset(int offset)
{
    int i=0;
    while(i<MAX_FREE)
    {
        if(repo_handle.free_list[i]==offset)
        {
            struct PDS_NdxInfo *ndx_entry;
            int key;

            fseek(repo_handle.pds_data_fp,offset,SEEK_SET);
            fread(&key,sizeof(int),1,repo_handle.pds_data_fp);
            fseek(repo_handle.pds_data_fp,0,SEEK_END);	

            ndx_entry = (struct PDS_NdxInfo *)malloc(sizeof(struct PDS_NdxInfo *)); 
            ndx_entry->key=key;
            ndx_entry->offset=offset;

            int status = bst_add_node(&repo_handle.pds_bst,key,ndx_entry);
            if(status != BST_SUCCESS)
            {
                free(ndx_entry);
                return PDS_UNDELETE_FAILED;
            }
            else
            {
                repo_handle.free_list[i]=-1;
                return PDS_SUCCESS;
            }
        }
        i++;
    }
    return PDS_UNDELETE_FAILED;
}



