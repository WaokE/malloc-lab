/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "WaokE",
    /* First member's full name */
    "HyunJun Lee",
    /* First member's email address */
    "dlgudwns1207@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

// 상수와 매크로 정의
#define WSIZE 8
#define DSIZE 16
#define CHUNKSIZE (1<<12) // 기본 크기 
#define MAX(x, y) ((x) > (y)? (x) : (y)) 
#define PACK(size, alloc) ((size) | (alloc)) // 크기와 할당비트를 OR 비트연산하여 헤더와 풋터에 저장할 수 있는 값을 만듬
#define GET(p) (*(unsigned int *)(p)) // 주소의 word를 읽음
#define PUT(p, val) (*(unsigned int *)(p) = (val)) // 주소에 word를 씀
#define GET_SIZE(p) (GET(p) & ~0x7) // 주소의 사이즈를 읽음(블록의 헤더 or 풋터에 있는)
#define GET_ALLOC(p) (GET(p) & 0x1) // 주소의 할당 비트를 읽음(블록의 헤더 or 풋터에 있는)
#define HDRP(bp) ((char *)(bp) - WSIZE) // 블록의 주소를 받아, 헤더의 주소를 계산
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // 블록의 주소를 받아, 풋터의 주소를 계산 
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // 블록의 주소를 받아, 다음 블록의 주소를 계산
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // 블록의 주소를 받아, 이전 블록의 주소를 계산

// 함수 원형들
int mm_init(void);
static void *extend_heap(size_t words);
void mm_free(void *bp);
static void *coalesce(void *bp);
void *mm_malloc(size_t size);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
void *mm_realloc(void *ptr, size_t size);

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 
 * mm_init - initialize the malloc package.
 */
static char *heap_listp;
static void *recently_allocated = NULL; 
int mm_init(void)
{   
    // 힙 생성과 유효성 검사
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1){
        return -1;
    }
    
    // 힙 초기화
    PUT(heap_listp, 0); // 미사용 패딩 워드
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // Prologue Block Header
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // Prologue Block Footer
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); // Epilogue Block Header
    heap_listp += (2*WSIZE); // 작업할 위치로 이동
    
    // 너무 큰 사이즈를 초기화 하려한다면 
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL){
        return -1;
    }
    return 0;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    
    // 힙을 size만큼 확장. 메모리가 부족하면 NULL 리턴
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1){
        return NULL;
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}

void mm_free(void *bp)
{
    // 블록의 사이즈를 읽어들여 저장
    size_t size = GET_SIZE(HDRP(bp));

    // 블록의 헤더와 푸터에 할당되지 않음 상태와 사이즈를 담아줌
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

static void *coalesce(void *bp)
{   
    // 타겟 블록의 직전 블록, 직후 블록의 할당 여부를 저장
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    // 타겟 블록의 사이즈를 저장
    size_t size = GET_SIZE(HDRP(bp));

    // Case 1 - 이전 블록과 다음 블록 모두 할당되어 있음
    if (prev_alloc && next_alloc) {
        return bp; // 변동 없이 블록의 포인터 리턴
    }

    // Case 2 - 이전 블록은 할당되어 있지만, 다음 블록은 할당되지 않음
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // 다음 블록의 사이즈를 확인하여 그만큼 사이즈를 늘려준다.
        PUT(HDRP(bp), PACK(size, 0)); // 블록의 헤더에 할당되지 않음 상태와 사이즈를 담아줌
        PUT(FTRP(bp), PACK(size, 0)); // 블록의 푸터에 할당되지 않음 상태와 사이즈를 담아줌
    }

    // Case 3 - 이전 블록은 할당되어 있지 않지만, 다음 블록은 할당되어 있음
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))); // 이전 블록의 사이즈를 확인하여 그만큼 사이즈를 늘려준다.
        PUT(FTRP(bp), PACK(size, 0)); // 블록의 푸터에 할당되지 않음 상태와 사이즈를 담아줌
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // 이전 블록의 헤더에 할당되지 않음 상태와 사이즈를 담아줌
        bp = PREV_BLKP(bp); // 블록의 포인터를 이전 블록으로 옮김
    }

    // Case 4 - 이전 블록과 다음 블록 모두 할당되어 있지 않음
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); // 이전 블록과 다음 블록의 사이즈를 더한 값만큼 사이즈를 늘려줌
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // 이전 블록의 헤더에 할당되지 않음 상태와 사이즈를 담아줌
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // 다음 블록의 푸터에 할당되지 않음 상태와 사이즈를 담아줌
        bp = PREV_BLKP(bp); // 블록의 포인터를 이전 블록으로 옮김
    }
    recently_allocated = bp;
    return bp; // 블록의 포인터를 리턴
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    // 의미없는 사이즈가 들어올시 NULL 리턴
    if (size == 0){return NULL;}
    
    // 입력받은 사이즈에 따라 헤더와 푸터를 위한 공간을 할당하고, 더블 워드 조건을 만족시킨다.
    if (size <= DSIZE){asize = 2*DSIZE;}
    else{asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);}

    // 할당할 곳을 탐색하고, 성공적으로 찾아냈다면 메모리를 할당해준다.
    if ((bp = find_fit(asize)) != NULL){
        place(bp, asize);
        return bp;
    }
    
    // 
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL){return NULL;}
    place(bp, asize);
    return bp;
}

static void *find_fit(size_t asize){
    void *bp;

    if (recently_allocated == NULL) {
        recently_allocated = heap_listp;
    }

    for (bp = recently_allocated; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
         if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            recently_allocated = bp;
            return bp;
            }
    }

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
         if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            recently_allocated = bp;
            return bp;
            }
    }

    return NULL;
}

static void place(void *bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp));

    if ((csize - asize) >= (2*DSIZE)){
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    }
    else{
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}














