#include "mospf_daemon.h"
#include "mospf_proto.h"
#include "mospf_nbr.h"
#include "mospf_database.h"

#include "ip.h"

#include "list.h"
#include "log.h"
#include "packet.h"
#include "arp.h"
#include "rtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

extern ustack_t *instance;

pthread_mutex_t mospf_lock;
int nbr_changed;

void mospf_init()
{
	pthread_mutex_init(&mospf_lock, NULL);

	instance->area_id = 0;
	// get the ip address of the first interface
	iface_info_t *iface = list_entry(instance->iface_list.next, iface_info_t, list);
	instance->router_id = iface->ip;
	instance->sequence_num = 0;
	instance->lsuint = MOSPF_DEFAULT_LSUINT;
	nbr_changed = 0;

	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		iface->helloint = MOSPF_DEFAULT_HELLOINT;
		init_list_head(&iface->nbr_list);
	}

	init_mospf_db();
}

void *sending_mospf_hello_thread(void *param);
void *sending_mospf_lsu_thread(void *param);
void *checking_nbr_thread(void *param);

void mospf_run()
{
	pthread_t hello, lsu, nbr;
	pthread_create(&hello, NULL, sending_mospf_hello_thread, NULL);
	pthread_create(&lsu, NULL, sending_mospf_lsu_thread, NULL);
	pthread_create(&nbr, NULL, checking_nbr_thread, NULL);
}

void *sending_mospf_hello_thread(void *param)
{
	char * packet;
	int time = 0;
	//fprintf(stdout, "TODO: send mOSPF Hello message periodically.\n");
	while(1){
		sleep(MOSPF_DEFAULT_HELLOINT);
		pthread_mutex_lock(&mospf_lock);
		//fprintf(stdout, "sending hello message\n");
		packet = (char * )malloc(ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE);
		struct ether_header * eth = (struct ether_header *)packet;
		struct iphdr *ip = packet_to_ip_hdr(packet);
		struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_BASE_HDR_SIZE);
		struct mospf_hello *hello = (struct mospf_hello *)((char *)mospf + MOSPF_HDR_SIZE);
		eth->ether_dhost[0] = 0x01;
		eth->ether_dhost[1] = 0x00;
		eth->ether_dhost[2] = 0x5E;
		eth->ether_dhost[3] = 0x00;
		eth->ether_dhost[4] = 0x00;
		eth->ether_dhost[5] = 0x05;
		eth->ether_type = htons(ETH_P_IP);

		
		ip->version = 4;
		ip->ihl = 5;
		ip->tos = 0;
		ip->tot_len = htons(IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE);
		ip->id = htons(0);
		ip->frag_off = 0;
		ip->ttl = DEFAULT_TTL;
		ip->protocol = 90;
		ip->daddr = htonl(0xE0000005);
		//memset(ip, 0, IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE);
		
		mospf->version = MOSPF_VERSION;
		mospf->type = MOSPF_TYPE_HELLO;
		mospf->len = htons(MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE);
		mospf->rid = htonl(instance->router_id);
		mospf->aid = htonl(instance->area_id);
		mospf->padding = htons(0);
		
		hello->helloint = htons(MOSPF_DEFAULT_HELLOINT);
		hello->padding = htons(0);
		iface_info_t * iface;
		list_for_each_entry(iface, &instance->iface_list, list){
			char * iface_packet = (char * )malloc(ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE);
			memcpy(iface_packet, packet, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE);
			struct ether_header * eth = (struct ether_header *)iface_packet;
			struct iphdr *ip = (struct iphdr *)((char *)iface_packet + ETHER_HDR_SIZE);
			struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_BASE_HDR_SIZE);
			struct mospf_hello *hello = (struct mospf_hello *)((char *)mospf + MOSPF_HDR_SIZE);
			hello->mask = htonl(iface->mask);
			mospf->checksum = mospf_checksum(mospf);
			ip->saddr = htonl(iface->ip);
			ip->checksum = ip_checksum(ip);
			memcpy(eth->ether_shost, iface->mac, 6);
			iface_send_packet(iface, iface_packet, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE);
		}
		free(packet);
		if((++time % 4) == 0){
			fprintf(stdout,"dumping message:\n");
			/*mospf_db_entry_t * db;
			list_for_each_entry(db, &mospf_db, list){
				fprintf(stdout,	"rid : ");
				fprintf(stdout,	IP_FMT, HOST_IP_FMT_STR(db->rid));
				fprintf(stdout,	" seq : %d, nadv : %d, neighbors:\n", db->seq, db->nadv);
				struct mospf_lsa * lsa = db->array;
				for(int i = 0; i < db->nadv; i++){
					fprintf(stdout,	"port ip: ");
					fprintf(stdout,	IP_FMT, HOST_IP_FMT_STR(lsa->subnet));
					fprintf(stdout,	"\t mask : ");
					fprintf(stdout,	IP_FMT, HOST_IP_FMT_STR(lsa->mask));
					fprintf(stdout,	"\n");
					lsa++;
				}
				fprintf(stdout,	"\t\n-------------------------------------------------------\n");		
			}*/
			pthread_mutex_lock(&rtable_lock);
			print_rtable();
			pthread_mutex_unlock(&rtable_lock);
		}
		pthread_mutex_unlock(&mospf_lock);
	}
		
		
		

	return NULL;
}

void *checking_nbr_thread(void *param)
{
	//fprintf(stdout, "TODO: neighbor list timeout operation.\n");
	mospf_nbr_t * nbr, * nbr1;
	iface_info_t * iface;
	while(1){
		sleep(1);
		pthread_mutex_lock(&mospf_lock);
		list_for_each_entry(iface, &instance->iface_list, list){
			list_for_each_entry_safe(nbr, nbr1, &iface->nbr_list, list){
					if(--nbr->alive == 0){
						list_delete_entry((struct list_head *)nbr);	
						iface->num_nbr--;
						nbr_changed = 1;
					}
			}
		}
		pthread_mutex_unlock(&mospf_lock);
	}

	return NULL;
}

void handle_mospf_hello(iface_info_t *iface, const char *packet, int len)
{
	//fprintf(stdout, "TODO: handle mOSPF Hello message.\n");
	//struct ether_header * eth = (struct ether_header *)packet;
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_BASE_HDR_SIZE);
	struct mospf_hello *hello = (struct mospf_hello *)((char *)mospf + MOSPF_HDR_SIZE);

	mospf_nbr_t * nbr;
	pthread_mutex_lock(&mospf_lock);
	list_for_each_entry(nbr, &iface->nbr_list, list){
		if(nbr->nbr_id == ntohl(mospf->rid)){
			nbr->alive = MOSPF_NEIGHBOR_TIMEOUT;
			pthread_mutex_unlock(&mospf_lock);
			return;
		}
	}
	
	nbr = (mospf_nbr_t *)malloc(sizeof(mospf_nbr_t));
	init_list_head(&nbr->list);
	nbr->nbr_id = ntohl(mospf->rid);
	nbr->nbr_ip = ntohl(ip->saddr);
	nbr->nbr_mask = ntohl(hello->mask);
	nbr->alive = MOSPF_NEIGHBOR_TIMEOUT;
	list_add_tail((struct list_head *)nbr, &iface->nbr_list);
	iface->num_nbr++;
	nbr_changed = 1;
	//generate_rt();
	pthread_mutex_unlock(&mospf_lock);
	return;
	
}

void *sending_mospf_lsu_thread(void *param)
{
	//fprintf(stdout, "TODO: send mOSPF LSU message periodically.\n");
	char * packet;
	iface_info_t * iface;
	mospf_db_entry_t * db;
	mospf_nbr_t * nbr;
	static int left_interval = MOSPF_DEFAULT_LSUINT;
	while(1){
		pthread_mutex_lock(&mospf_lock);
		while(!nbr_changed && --left_interval){
			pthread_mutex_unlock(&mospf_lock);
			sleep(1);
			pthread_mutex_lock(&mospf_lock);
		}
		nbr_changed = 0;
		left_interval = MOSPF_DEFAULT_LSUINT;
		//gather nbr informations, save it to database
		struct mospf_lsa * lsa_list;
		struct mospf_lsa * lsa_tmp;
		int nadv = 0;
		list_for_each_entry(iface, &instance->iface_list, list){
			if(iface->num_nbr == 0)
				nadv += 1;
			else
				nadv += iface->num_nbr;
		}

		lsa_list = (struct mospf_lsa *)malloc(MOSPF_LSA_SIZE * nadv);
		lsa_tmp = lsa_list;
		list_for_each_entry(iface, &instance->iface_list, list){
			if(list_empty(&iface->nbr_list)){
				lsa_tmp->subnet = iface->ip & iface->mask;
				lsa_tmp->mask = iface->mask;
				lsa_tmp->rid = 0;
				lsa_tmp++;
			}
			list_for_each_entry(nbr, &iface->nbr_list, list){
				lsa_tmp->subnet = nbr->nbr_ip & nbr->nbr_mask;
				lsa_tmp->mask = nbr->nbr_mask;
				lsa_tmp->rid = nbr->nbr_id;
				lsa_tmp++;
			}
		}
		
		int found = 0;
		list_for_each_entry(db, &mospf_db, list){
		 	if(db->rid == instance->router_id){
				found = 1;
				db->array = lsa_list;
				db->seq = instance->sequence_num;
				db->nadv = nadv;
				break;
			}
		}

		if(!found){
			db = (mospf_db_entry_t *)malloc(sizeof(mospf_db_entry_t));
			db->array = lsa_list;
			init_list_head(&db->list);
			db->nadv = nadv;
			db->rid = instance->router_id;
			db->seq = instance->sequence_num;
			list_add_tail((struct list_head *) db, &mospf_db);
		}

		//make lsu packet
		packet = (char * )malloc(ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + MOSPF_LSA_SIZE * nadv);
		struct ether_header * eth = (struct ether_header *)packet;
		struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
		struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_BASE_HDR_SIZE);
		struct mospf_lsu *lsu = (struct mospf_lsu *)((char *)mospf + MOSPF_HDR_SIZE);
		struct mospf_lsa *lsa = (struct mospf_lsa *)((char *)lsu + MOSPF_LSU_SIZE);

		eth->ether_type = htons(ETH_P_IP);
		
		ip->version = 4;
		ip->ihl = 5;
		ip->tos = 0;
		ip->tot_len = htons(IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + MOSPF_LSA_SIZE * nadv);
		ip->id = rand();
		ip->frag_off = htons(IP_DF);
		ip->ttl = DEFAULT_TTL;
		ip->protocol = 90;
		
		mospf->version = MOSPF_VERSION;
		mospf->type = MOSPF_TYPE_LSU;
		mospf->rid = htonl(instance->router_id);
		mospf->aid = htonl(instance->area_id);
		mospf->padding = htons(0);
		mospf->len = htons(MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + MOSPF_LSA_SIZE * nadv);
		
		lsu->seq = htons(++instance->sequence_num);
		lsu->ttl = MOSPF_MAX_LSU_TTL;
		lsu->unused = 0;
		lsu->nadv = htonl(nadv);

		lsa_tmp = lsa_list;
		for(int i = nadv; i > 0; i--, lsa++, lsa_tmp++){
			lsa->subnet = htonl(lsa_tmp->subnet);
			lsa->mask = htonl(lsa_tmp->mask);
			lsa->rid = htonl(lsa_tmp->rid);
		}

		mospf->checksum = mospf_checksum(mospf);

		//for each neighbor send packet
		list_for_each_entry(iface, &instance->iface_list, list){
			list_for_each_entry(nbr, &iface->nbr_list, list){
				char * iface_packet = (char * )malloc(ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + MOSPF_LSA_SIZE * nadv);
				memcpy(iface_packet, packet, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + MOSPF_LSA_SIZE * nadv);
				struct ether_header * eth = (struct ether_header *)iface_packet;
				struct iphdr *ip = (struct iphdr *)((char *)iface_packet + ETHER_HDR_SIZE);
				//struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_BASE_HDR_SIZE);
				//struct mospf_lsu *lsu = (struct mospf_lsu *)((char *)mospf + MOSPF_HDR_SIZE);
				//struct mospf_lsa *lsa = (struct mospf_lsa *)((char *)lsu + MOSPF_LSU_SIZE);

				ip->saddr = htonl(iface->ip);
				ip->daddr = htonl(nbr->nbr_ip);
				ip->checksum = ip_checksum(ip);
				memcpy(eth->ether_shost, iface->mac, 6);
				iface_send_packet_by_arp(iface, nbr->nbr_ip, iface_packet, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + MOSPF_LSA_SIZE * nadv);
			}
		}
		free(packet);
		generate_rt();
		pthread_mutex_unlock(&mospf_lock);
	}

	return NULL;
}

void handle_mospf_lsu(iface_info_t *iface, char *packet, int len)
{
	//fprintf(stdout, "TODO: handle mOSPF LSU message.\n");
	mospf_db_entry_t * db;
	mospf_nbr_t * nbr;
	struct ether_header * eth = (struct ether_header *)packet;
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_BASE_HDR_SIZE);
	struct mospf_lsu *lsu = (struct mospf_lsu *)((char *)mospf + MOSPF_HDR_SIZE);
	struct mospf_lsa *lsa = (struct mospf_lsa *)((char *)lsu + MOSPF_LSU_SIZE);
	pthread_mutex_lock(&mospf_lock);

	int found = 0;
	list_for_each_entry(db, &mospf_db, list){
		if(db->rid == ntohl(mospf->rid)){
			found = 1;
			break;
		}
	}
	
	if(!found){
		db = (mospf_db_entry_t *)malloc(sizeof(mospf_db_entry_t));
		init_list_head(&db->list);
		db->rid = ntohl(mospf->rid);
		db->seq = 0;
		list_add_tail((struct list_head *) db, &mospf_db);
	}

	if(db->seq < ntohs(lsu->seq)){
		db->seq = ntohs(lsu->seq);
		db->nadv = ntohl(lsu->nadv);
		db->array = (struct mospf_lsa *)malloc(MOSPF_LSA_SIZE * ntohl(lsu->nadv));
		struct mospf_lsa * lsa_tmp = db->array;
		for(int i = ntohl(lsu->nadv); i > 0; i--, lsa++, lsa_tmp++){
			lsa_tmp->subnet = ntohl(lsa->subnet);
			lsa_tmp->mask = ntohl(lsa->mask);
			lsa_tmp->rid = ntohl(lsa->rid);
		}
		//generate_rt();
	}	
	else{
		pthread_mutex_unlock(&mospf_lock);
		return;
	}
	
	lsu->ttl--;
	ip->ttl--;
	mospf->checksum = mospf_checksum(mospf);
	iface_info_t * iface_tmp;
	list_for_each_entry(iface_tmp, &instance->iface_list, list){
		if(iface_tmp != iface){
			list_for_each_entry(nbr, &iface_tmp->nbr_list, list){
				char * iface_packet = (char * )malloc(ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + MOSPF_LSA_SIZE * db->nadv);
				struct iphdr *ip = (struct iphdr *)((char *)iface_packet + ETHER_HDR_SIZE);
				memcpy(iface_packet, packet, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + MOSPF_LSA_SIZE * db->nadv);
				ip->daddr = htonl(nbr->nbr_ip);
				ip->saddr = htonl(iface_tmp->ip);
				ip->checksum = ip_checksum(ip);
				memcpy(eth->ether_shost, iface_tmp->mac, 6);
				iface_send_packet_by_arp(iface_tmp, nbr->nbr_ip, iface_packet, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + MOSPF_LSA_SIZE * db->nadv);
			}
		}
	}

	pthread_mutex_unlock(&mospf_lock);
	
}

void handle_mospf_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_HDR_SIZE(ip));

	if (mospf->version != MOSPF_VERSION) {
		log(ERROR, "received mospf packet with incorrect version (%d)", mospf->version);
		return ;
	}
	if (mospf->checksum != mospf_checksum(mospf)) {
		log(ERROR, "received mospf packet with incorrect checksum");
		return ;
	}
	if (ntohl(mospf->aid) != instance->area_id) {
		log(ERROR, "received mospf packet with incorrect area id");
		return ;
	}

	// log(DEBUG, "received mospf packet, type: %d", mospf->type);

	switch (mospf->type) {
		case MOSPF_TYPE_HELLO:
			handle_mospf_hello(iface, packet, len);
			break;
		case MOSPF_TYPE_LSU:
			handle_mospf_lsu(iface, packet, len);
			break;
		default:
			log(ERROR, "received mospf packet with unknown type (%d).", mospf->type);
			break;
	}
}

iface_info_t * gw_to_iface(u32 gw){
	iface_info_t * iface;
	mospf_nbr_t * nbr;
	list_for_each_entry(iface, &instance->iface_list, list){
		list_for_each_entry(nbr, &iface->nbr_list, list){
			if(nbr->nbr_id == gw )
				return iface;
		}
	}
	return NULL;
}

iface_info_t * subnet_to_iface(u32 subnet){
	iface_info_t * iface;
	list_for_each_entry(iface, &instance->iface_list, list){
		
		if((iface->ip & iface->mask )== subnet )
			return iface;
		
	}
	return NULL;
}


void generate_rt(){
	pthread_mutex_lock(&rtable_lock);
	clear_rtable();
	init_rtable();

	struct dist_entry * dist;
	int rnum = 0;
	int i,j,k;
	int min_dist;
	int min_j;
	//u32 tmp_rid;
	mospf_db_entry_t * db, * db1, * self_db = NULL;
	struct mospf_lsa * lsa;
	//mospf_nbr_t * nbr;
	
	list_for_each_entry(db, &mospf_db, list){
		rnum++;
		if(db->rid == instance->router_id){
			self_db = db;
			//fprintf(stdout,"self db assigned\n");
		}
	}
	if(!self_db){
		pthread_mutex_unlock(&rtable_lock);
		return;
	}

	dist = (struct dist_entry * )malloc((rnum + 1)*sizeof(struct dist_entry));
	struct dist_entry * tmp = dist;
	u32 graph[rnum][rnum];
	memset(graph, 0, rnum*rnum*sizeof(u32));
	//fprintf(stdout,"phase init router dist list\n");

	//init router dist list
	list_for_each_entry(db, &mospf_db, list){
		tmp->rid = db->rid;
		tmp->visited = 0;
		tmp->dist = MAX_DIST;
		tmp->gw = BAD_GW;
		if(tmp->rid == instance->router_id){
			//fprintf(stdout,"phase init router dist list if\n");
			tmp->dist = 0;
			tmp->visited = 1;
			tmp->gw = 0;
		}
		else{
			//fprintf(stdout,"phase init router dist list else\n");
			for(i = 0; i < self_db->nadv; i++){
				if(tmp->rid == self_db->array[i].rid){
					tmp->dist = 1;
					tmp->gw = tmp->rid;
					break;
				}
			}
		}
		
		tmp++;
	}

	//fprintf(stdout,"phase init graph\n");
	//init graph(containing edges and distances between neighbours)
	k = 0;
	list_for_each_entry(db, &mospf_db, list){
		lsa = db->array;
		for(i = 0; i < db->nadv; i++){
			j = 0;
			list_for_each_entry(db1, &mospf_db, list){
				if(lsa[i].rid == db1->rid){
					break;
				}
				else
					j++;
			}
			graph[k][j] = 1;
		}
		k++;
	}

	//fprintf(stdout,"phase main loop\n");
	//main loop of dijkstra algorithm
	for(i = 0; i < rnum - 1; i++ ){
		min_dist = MAX_DIST;
		min_j = 0;

		//find closest unvisited router
		for(j = 0; j < rnum; j++){
			
			//if not visited
			if(dist[j].visited == 0){
				if(dist[j].dist < min_dist){
					min_dist = dist[j].dist;
					min_j = j;
				}
			}
		}

		dist[min_j].visited = 1;

		//change the distance of min_j's neighbours
		for(j = 0; j < rnum; j++){
			if(graph[min_j][j] && dist[j].visited == 0 && graph[min_j][j] + dist[min_j].dist < dist[j].dist){
				dist[j].dist = graph[min_j][j] + dist[min_j].dist;
				dist[j].gw = (dist[min_j].gw) ? dist[min_j].gw: dist[min_j].rid;
			}
		}
	}

	//fprintf(stdout,"phase transfer\n");
	rt_entry_t * rt;
	j = 0;
	int found = 0;
	list_for_each_entry(db, &mospf_db, list){
		for(i = 0; i < db->nadv; i++){
			found = 0;
			list_for_each_entry(rt, &rtable, list){
				if(db->array[i].subnet == rt->dest ){
					found = 1;
					if(rt->dist > dist[j].dist){
						rt->dist = dist[j].dist;
						rt->gw = dist[j].gw;
						rt->iface = (dist[j].gw) ? gw_to_iface(dist[j].gw) : subnet_to_iface(db->array[i].subnet);
						if(!rt->iface){
							log(WARNING,"gw to iface miss, within transfer found loop");
							break;
						}
						rt->mask = rt->iface->mask;
						break;
						
					}
					//log(DEBUG,"found and not exchange,ip "IP_FMT" gw "IP_FMT" %d",HOST_IP_FMT_STR(db->array[i].subnet),HOST_IP_FMT_STR(dist[j].gw),dist[j].visited);
					break;
				}
			}

			
			if(!found){
				//log(DEBUG,"not found,ip "IP_FMT" gw "IP_FMT" %d",HOST_IP_FMT_STR(db->array[i].subnet),HOST_IP_FMT_STR(dist[j].gw),dist[j].visited);
				if(!gw_to_iface(dist[j].gw)){
					if(dist[j].gw != 0){
						log(WARNING,"gw to iface miss, within transfer not fdound loop, gw:"IP_FMT"  "IP_FMT" %d",HOST_IP_FMT_STR(dist[j].gw),HOST_IP_FMT_STR(dist[j].rid),dist[j].visited);
						continue;
					}
					rt = new_rt_entry(db->array[i].subnet, db->array[i].mask, 0, subnet_to_iface(db->array[i].subnet));
					add_rt_entry(rt);
					continue;
				}
				rt = new_rt_entry(db->array[i].subnet, ((dist[j].gw) ? gw_to_iface(dist[j].gw) : subnet_to_iface(db->array[i].subnet))->mask, dist[j].gw, (dist[j].gw) ? gw_to_iface(dist[j].gw) : subnet_to_iface(db->array[i].subnet));
				add_rt_entry(rt);
				rt->dist = dist[j].dist;
			}
			
		}
				
		j++;
	}
	pthread_mutex_unlock(&rtable_lock);
}


