/*************************************************************************
    > File Name: fast_ip_table_match.c
    > Author: wushenxiaohuai
    > Mail: 741466738@qq.com
    > Created Time: Sat 05 May 2018 06:59:45 AM DST
 ************************************************************************/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
#include<stdint.h>
#include<sys/time.h>
#include<assert.h>

#define u32 uint32_t
#define MATCH_TIMES 10
#define leaf(root) ((root->son0 == NULL) && (root->son1 == NULL))

typedef struct basic_tree_node{
	struct basic_tree_node * son0;
	struct basic_tree_node * son1;
	int matched;
	u32 prefix;
	u32 mask;
} btn;

typedef struct new_tree_node{
	uint16_t bits;
	struct new_tree_node ** ina;   //array for intrenal node
	u32 * lna;   //array for leaf node prefix
} ntn;

btn * last_matched;
u32 ip_array[MATCH_TIMES];
u32 mask_array[MATCH_TIMES];

btn * btn_init(btn * son0, btn * son1, int matched)
{
	btn * tmp = (btn *)malloc(sizeof(btn));
	tmp->son0 = son0;
	tmp->son1 = son1;
	tmp->matched = matched;
	tmp->prefix = 0;
	tmp->mask = 0;
	return tmp;
}

ntn * ntn_init()
{
	ntn * root = (ntn * )malloc(sizeof(ntn));
	root->bits = 0;
	root->ina = NULL;
	root->lna = NULL;
}


void bt_add_node(btn * root, u32 ip, u32 mask, u32 start)
{
	if(start <= mask)
	{
		int j = ip & (1<<(32 - start));
		//printf("start %d, j %d, %x, ip %x\n", start, j, 1<<(32-start), ip);
		if(j)
		{
			if(!root->son1)
				root->son1 = btn_init(NULL, NULL, 0);
			bt_add_node( root->son1, ip, mask, start + 1);
		}
		else
		{
			if(!root->son0)
				root->son0 = btn_init(NULL, NULL, 0);
			bt_add_node( root->son0, ip, mask, start + 1);
		}
	}
	else
	{
		root->matched = 1;
		root->prefix = ip;
		root->mask = mask;
	}
}

int bt_match(btn * root, u32 ip, u32 mask, u32 start)
{
	if (root->matched)
		last_matched = root;
	if(start <= mask)
	{
		int j = ip & (1<<(32 - start));
		//printf("start %d, j %d, %x, ip %x\n", start, j, 1<<(32-start), ip);
		if(j)
		{
			if(!root->son1)
			{
				//printf("son1 empty ip %x mask %d start %d, last match : %x %d\n", ip, mask, start,(last_matched)?last_matched->prefix:0, (last_matched)?last_matched->mask:0);
				return root->matched;
			}
			return bt_match( root->son1, ip, mask, start + 1);
		}
		else
		{
			if(!root->son0)
			{
				//printf("son0 empty ip %x mask %d start %d, last match : %x %d\n", ip, mask, start,(last_matched)?last_matched->prefix:0, (last_matched)?last_matched->mask:0);
				return root->matched;
			}
			return bt_match( root->son0, ip, mask, start + 1);
		}
	}
	else
	{
		//printf("shoot ip %x mask %d start %d, current : %x %d\n", ip, mask, start,root->prefix, root->mask);
		return root->matched;
	}
}


btn * basic_prefix_match()
{
	printf("start basic tree build\n");
	FILE * fp = fopen("forwarding-table.txt","r");
	btn * root = btn_init(NULL, NULL, 0);
	u32 ip0, ip1, ip2, ip3, ip;
	u32 mask;
	while(!feof(fp))
	{
		fscanf(fp, "%u.%u.%u.%u %d %d\n", &ip0, &ip1, &ip2, &ip3, &mask, &ip);
		ip = (ip0<<24) + (ip1<<16) + (ip2<<8) + ip3;
		//printf("entry : ip : %x, %d\n", ip, mask);
		bt_add_node( root, ip, mask, 1);
	}
	printf("tree built\n");
	struct timeval tv_start;
	struct timezone tz_start;
	struct timeval tv_end;
	struct timezone tz_end;
	int i = MATCH_TIMES;
	int matched;
	while(i-- > 0)
	{
		ip_array[i] = rand(); 
		//ip_array[i] = 0x6b8b4567;
		mask_array[i] = rand()%8 + 24;
		//mask_array[i] = 30;
		gettimeofday(&tv_start,&tz_start);
		last_matched = NULL;
		matched = (last_matched)? 1:bt_match(root,ip_array[i], mask_array[i], 1);
		gettimeofday(&tv_end,&tz_end);
		printf("matching ip : %x, %d, state : %s, time: %ld usec\n", ip_array[i], mask_array[i], (matched)?"matched":"unmatched",1000000*(tv_end.tv_sec - tv_start.tv_sec) + tv_end.tv_usec - tv_start.tv_usec);
	} 
	return root;
}

void leaf_pushing(btn * root, u32 prefix, u32 mask)
{
	if(mask > root->mask)
	{
		root->matched = 1;
		root->mask = mask;
		root->prefix = prefix;
	}
	if(leaf(root))
		return;
	if(!root->son0)
		root->son0 = btn_init(NULL, NULL,0);
	leaf_pushing(root->son0, root->prefix, root->mask);
	if(!root->son1)
		root->son1 = btn_init(NULL,NULL,0);
	leaf_pushing(root->son1, root->prefix, root->mask);
}

ntn * tree_transfer(btn * root,int bit, int original_bit)
{
	ntn * nroot = ntn_init();
	if(bit == 1)
	{
		if(leaf(root))
		{
			nroot->lna = (u32 *) malloc( 2*sizeof(u32));
			nroot->lna[0] = root->prefix;
			nroot->lna[1] = root->prefix;
			nroot->bits = 0;
			return nroot;
		}
		else
		{
			uint16_t l = !leaf(root->son0);
			uint16_t r = !leaf(root->son1);
			nroot->bits = (l<<15) + (r<<14);
			if(nroot->bits & ((1<<14) - 1))
				printf("ERROR: bits: %x,l %d, r %d, bit %d\n",nroot->bits,l, r, bit);
			if(l + r == 2)
			{
				nroot->ina = (ntn **) malloc(2 * sizeof(ntn *));
				nroot->ina[0] = tree_transfer(root->son0,original_bit, original_bit);
				nroot->ina[1] = tree_transfer(root->son1, original_bit, original_bit);
				return nroot;
			}
			else if(l + r == 1)
			{
				nroot->ina = (ntn **) malloc(sizeof(ntn*));
				nroot->lna = (u32 *) malloc(sizeof(u32));
				if(l)
				{
					nroot->ina[0] = tree_transfer(root->son0, original_bit, original_bit);
					nroot->lna[0] = root->son1->prefix;
				}
				else
				{
					nroot->ina[0] = tree_transfer(root->son1, original_bit, original_bit);
					nroot->lna[0] = root->son0->prefix;
				}
				return nroot;
			}
			else
			{
				nroot->lna = (u32 *) malloc( 2*sizeof(u32));
				nroot->lna[0] = root->son0->prefix;
				nroot->lna[1] = root->son1->prefix;
				return nroot;
			}
		}
	}
	else
	{
		if(leaf(root))
		{
			nroot->lna = (u32 *) malloc((1<<bit) *sizeof(u32));
			nroot->bits = 0;
			for(int i = 0; i < (1 << bit); i++)
			{
				nroot->lna[i] = root->prefix;
			}
			return nroot;
		}
		else
		{
			int li, ll, ri, rl;
			ntn * left = tree_transfer(root->son0, bit - 1, original_bit);
			ntn * right = tree_transfer(root->son1, bit - 1, original_bit);
			li = __builtin_popcount(left->bits);
			ll = (1 << (bit - 1)) - li; 
			// if(left->bits & ((1<<14)-1))
				// printf("ERROR: wrong left bits , bits: %x, now bit %d\n", left->bits,  bit);
			ri = __builtin_popcount(right->bits);
			rl = (1 << (bit - 1)) - ri; 
			// if(right->bits & ((1<<14)-1))
				// printf("ERROR: wrong right bits , bits: %x, now bit %d\n", right->bits,  bit);
			if(li + ri)
			{
				nroot->ina = (ntn **)malloc((li+ri)*sizeof(ntn *));
				if(li)
				{
					//printf("memcpy for li\n");
					memcpy(nroot->ina, left->ina, li * sizeof(ntn *));
					assert(left->ina);
					free(left->ina);
				}
				if(ri)
				{
					//printf("memcpy for ri\n");
					memcpy(nroot->ina + li, right->ina, ri * sizeof(ntn *));
					assert(right->ina);
					free(right->ina);
				}
			}
			if(ll + rl)
			{
				nroot->lna = (u32 *)malloc((ll+rl)*sizeof(u32));
				if(ll)
				{
					//printf("memcpy for ll, nroot->lna %x, left->lna %x, ll %d, li %d, rl %d, ri %d\n", (u32)nroot->lna, (u32)left->lna, ll, li, rl ,ri);
					memcpy(nroot->lna, left->lna, ll * sizeof(u32));
					assert(left->lna);
					free(left->lna);
				}
				if(rl)
				{
					//printf("memcpy for rl\n");
					memcpy(nroot->lna + ll, right->lna, rl * sizeof(u32));
					assert(right->lna);
					free(right->lna);
				}
			}
			nroot->bits = left->bits + (right->bits >> (1 << (bit-1)));
			//if(nroot->bits & ((1<<12) - 1))
			//	printf("bits: %x,left %x, right %x, bit %d\n",nroot->bits,left->bits, right->bits, bit);
			free(left);
			free(right);
			return nroot;
		}
	}

}

u32 fast_match(ntn * root, u32 ip, u32 mask, int start,int bit)
{
	u32 idx = (ip << (start - 1 )) >> (32 - bit);
	assert(root);
	if(root->bits & (1 << (15 - idx)))
	{
		assert(root->ina);
		ntn * new_root = root->ina[__builtin_popcount(root->bits >> (16 - idx))];
		return fast_match( new_root, ip, mask, start + bit, bit);
	}
	else
	{
		assert(root->lna);
		return root->lna[idx - __builtin_popcount(root->bits >> (16 - idx))];
	}
}
	
void fast_prefix_match(char bit_num)
{
	btn * root = basic_prefix_match();
	leaf_pushing(root, 0, 0);
	
	ntn * fast_prefix_tree = tree_transfer(root, bit_num - '0', bit_num - '0');
	struct timeval tv_start;
	struct timezone tz_start;
	struct timeval tv_end;
	struct timezone tz_end;
	u32 mask, ip;
	int i = MATCH_TIMES;
	u32 matched;
	while(i-- > 0)
	{
		//ip = 0x2ae8944a;
		gettimeofday(&tv_start,&tz_start);
		matched = fast_match(fast_prefix_tree,ip_array[i], mask_array[i], 1, bit_num-'0');
		gettimeofday(&tv_end,&tz_end);
		printf("matching ip : %x, %d, state : %s, time: %ld usec\n", ip_array[i], mask_array[i], (matched)?"matched":"unmatched",1000000*(tv_end.tv_sec - tv_start.tv_sec) + tv_end.tv_usec - tv_start.tv_usec);
	}
}



int main(int argc, char * argv[])
{
	//if wrong options
	if(argc != 3)
	{
		printf("wrong options!\n");
		return -1;
	}
	char tmp = *argv[2];
	//start construct tree and random match
	if(*argv[1] == '0')
		basic_prefix_match();
	else
		fast_prefix_match(tmp);

	return 0;
}
