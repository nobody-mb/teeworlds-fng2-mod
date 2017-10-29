/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "string.h"
#include <new>
#include <base/math.h>
#include <engine/shared/config.h>
#include "gamecontext.h"
#include <game/gamecore.h>
#include "stats.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include "player.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

tstats::tstats (CGameContext *game_srv, const char *stats_dir)
{
	game_server = game_srv;
	stat_dir = stats_dir;
	m_pServer = game_srv->Server();
	
	memset(round_stats, 0, sizeof(round_stats));
	memset(round_names, 0, sizeof(round_names));
	memset(current, 0, sizeof(current));
	num_totals = 0;
	round_index = 0;
	ertimer = 0;
	
	struct dirent *ds;
	DIR *dp;
	
	if ((dp = opendir(stats_dir))) {
		while ((ds = readdir(dp)))
			++num_totals;
		closedir(dp);
	} else {
		printf("error reading stats\n");
	}
	last_reqd = (int)time(NULL);
	last_reqds = (int)time(NULL);
	
	max_totals = num_totals + 256;
	num_totals = 0;
	
	total_stats = (struct tee_stats *)calloc(max_totals, sizeof(struct tee_stats));
	total_names = (char **)calloc(max_totals, sizeof(char *));
	
	for (int i = 0; i < 512; i++)
		round_names[i] = (char *)calloc(sizeof(char), 64);
	
	if ((dp = opendir(stats_dir))) {
		while ((ds = readdir(dp)))
			if (*ds->d_name != '.') {
				total_stats[num_totals] = read_statsfile(ds->d_name, 0);
				total_names[num_totals] = strdup(ds->d_name);
				num_totals++;
			}
		closedir(dp);
	}
	
	printf("**** initialized stats object with %d total\n", num_totals);
}

tstats::~tstats()
{
	for (int i = 0; i < num_totals; i++)
		free(total_names[i]);
		
	free(total_names);
	free(total_stats);

	for (int i = 0; i < 512; i++)
		free(round_names[i]);
		
	printf("**** freed stats object with %d total\n", num_totals);
}

void tstats::on_enter (int ClientID, const char *name)
{
	struct tee_stats ts;
	memset(&ts, 0, sizeof(ts));
	ts.id = ClientID;
	ts.join_time = time(NULL);
	add_round_entry(ts, name);
}

void tstats::SendChat(int ChatterClientID, int Team, const char *pText)
{
	game_server->SendChat(ChatterClientID, Team, pText);
}

void tstats::SendChatTarget(int To, const char *pText)
{
	game_server->SendChatTarget(To, pText);
}

void tstats::on_namechange (int ClientID, const char *name)
{
	for (int i = 0; i < 512; i++) {
		if (round_stats[i].id == ClientID) {
			printf("client id %d found at %d name %s\n", ClientID, i,
				round_names[i]);
			strncpy(round_names[i], name, 64);
			break;
		}
	}
}

void tstats::on_drop (int ClientID, const char *pReason)
{
	struct tee_stats *t;

	if ((t = current[ClientID])) {
		t->spree = 0;	/* thanks SP | Someone :D */
		t->num_games++;
		t->id = -1;
		current[ClientID] = NULL;
	}
	
	if (t && pReason && *pReason) {
		char entry[256] = { 0 };
		char aIP[16] = { 0 };
		int fd, len;
	
		Server()->GetClientAddr(ClientID, aIP, sizeof(aIP));
		
		snprintf(entry, sizeof(entry), "%s left (%s) %s\n", ID_NAME(ClientID), 
			pReason, aIP);
		len = (int)strlen(entry);
			
		printf("%s", entry);
			
		if ((fd = open("leaving.txt", O_RDWR|O_CREAT|O_APPEND, 0777)) < 0)
			perror("open");
		else
			if (write(fd, entry, len) != len)
				perror("write");
			
		close(fd);
	}
}

double tstats::print_best_group (char *dst, struct tee_stats *stats, char **names, 
	int num, double (*callback)(struct tee_stats, char *), double max)
{
	int i, len;
	double kd = 0, best = 0;
	char tmp_buf[256] = { 0 };
	char call_buf[128] = { 0 };
	
	for (i = 0; i < num; i++) {
		if (!names[i][0])
			continue;
		kd = callback(stats[i], call_buf);
		if ((kd > best) && (kd < max))
			best = kd;
	}
	for (i = 0; i < num; i++) {
		if (!names[i][0])
			continue;
		memset(call_buf, 0, sizeof(call_buf));
		kd = callback(stats[i], call_buf);
		if (kd == best) {
			len = strlen(call_buf) + strlen(tmp_buf) + strlen(names[i]) + 5;
			if (len >= sizeof(tmp_buf))
				break;
			strcat(tmp_buf, names[i]);
			if (strlen(call_buf)) {
				strcat(tmp_buf, " (");
				strcat(tmp_buf, call_buf);
				strcat(tmp_buf, ")");
			}
			strcat(tmp_buf, ", ");
		}
	}
	
	tmp_buf[strlen(tmp_buf) - 2] = 0;

	if (callback == get_kd || callback == get_kd_all)
		sprintf(dst, "%.02f (%s)", best, ((best != 0) ? tmp_buf : "None"));
	else if (callback == get_accuracy || callback == get_accuracy_all)
		sprintf(dst, "%.02f%% (%s)", best, ((best != 0) ? tmp_buf : "None"));
	else if (callback == get_neg_steals)
		sprintf(dst, "%d (%s)", (int)(-best), ((best != 0) ? tmp_buf : "None"));
	else
		sprintf(dst, "%d (%s)", (int)best, ((best != 0) ? tmp_buf : "None"));
	
	return best;
}

#define PLACEHOLDER 9999999999

void tstats::print_best (const char *msg, int max, 
	double (*callback)(struct tee_stats, char *), int all)
{
	double tmp, best = PLACEHOLDER;
	char buf[256];
	char buf1[256] = { 0 };
	
	while (max--) {
		memset(buf, 0, sizeof(buf));
		if (all)
			tmp = print_best_group(buf, total_stats, total_names, 
				num_totals, callback, best);
		else
			tmp = print_best_group(buf, round_stats, round_names, 
				round_index, callback, best);
		if ((tmp >= best) || (best < PLACEHOLDER && tmp == 0))
			break;
		if (msg) {
			snprintf(buf1, sizeof(buf1), "- %s %s, ", msg, buf);
			msg = NULL;
		} else {
			if ((strlen(buf) + strlen(buf1)) > 50) {
				SendChat(-1, CGameContext::CHAT_ALL, buf1);
				memset(buf1, 0, sizeof(buf1));
				strcat(buf1, "     - ");
			}
			strcat(buf1, buf);
			strcat(buf1, ", ");
		}
		best = tmp;
		if (best == 0) 
			break;
	}
	
	if (strlen(buf1) > 3)
		buf1[strlen(buf1) - 2] = 0;
	
	SendChat(-1, CGameContext::CHAT_ALL, buf1);

}

void tstats::send_stats (const char *name, int req_by, struct tee_stats *ct, int is_all)
{
	char buf[256];
	int c, d, e;
	
	str_format(buf, sizeof(buf), "%s stats for '%s' (req. by '%s') client version: %d", 
		is_all ? "total" : "round", name, Server()->ClientName(req_by), ct->version);
	SendChat(-1, CGameContext::CHAT_ALL, buf);

	d = ct->deaths ? ct->deaths : 1;
	c = ct->kills + ct->kills_x2 + ct->kills_wrong;	
	e = ct->shots ? ct->shots : 1; 
	str_format(buf, sizeof(buf), "- k/d: %d/%d = %.03f | accuracy: %.03f%%", c, 
		ct->deaths, (float)c / (float)d, 100 * ((float)ct->freezes / (float)e));
	SendChat(-1, CGameContext::CHAT_ALL, buf);
	
	str_format(buf, sizeof(buf), "- net steals: %d - %d = %d", ct->steals, 
		ct->stolen_from, ct->steals - ct->stolen_from);
	SendChat(-1, CGameContext::CHAT_ALL, buf);	
	
	str_format(buf, sizeof(buf), "- avg ping: %d | shots: %d | wallshots: %d", 
		ct->avg_ping, ct->shots, ct->bounce_shots);
	SendChat(-1, CGameContext::CHAT_ALL, buf);
	
	str_format(buf, sizeof(buf), "- freezes: %d/%d | hammers: %d/%d | suicides: %d", 
		ct->freezes, ct->frozen, ct->hammers, ct->hammered, ct->suicides);
	SendChat(-1, CGameContext::CHAT_ALL, buf);

	if (!is_all) {
		time_t diff = (time(NULL) - ct->join_time);
		str_format(buf, sizeof(buf), 
			"- time: %d:%.02d | max spree: %d | max multi: %d:",
			diff / 60, diff % 60, ct->spree_max, ct->max_multi);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} else {
		str_format(buf, sizeof(buf), 
			"- wrong-shrine kills: %d | max spree: %d | max multi: %d:",
			ct->kills_wrong, ct->spree_max, ct->max_multi);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
		
		struct stat attrib;
		char date[64], path[64];
		snprintf(path, sizeof(path), "%s/%s", STATS_DIR, name);
   	 	if (stat(path, &attrib)) {
   	 		printf("error stat %s\n", path);
   	 		strncpy(date, "---", sizeof(date));
   	 	} else {
   	 		strftime(date, sizeof(date), "%F %r", 
   	 			localtime(&(attrib.st_mtime)));
   	 	}
   	 	str_format(buf, sizeof(buf), "- last seen: %s", date);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	}

/* ranks! */

	if (ct->multis[0]) {
		str_format(buf, sizeof(buf), "- ** double kills: %d", ct->multis[0]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} if (ct->multis[1]) {
		str_format(buf, sizeof(buf), "- ** triple kills: %d", ct->multis[1]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} if (ct->multis[2]) {
		str_format(buf, sizeof(buf), "- ** quad kills: %d", ct->multis[2]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} if (ct->multis[3]) {
		str_format(buf, sizeof(buf), "- ** penta kills: %d", ct->multis[3]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} if (ct->multis[4]) {
		str_format(buf, sizeof(buf), "- ** ultra kills: %d", ct->multis[4]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} if (ct->multis[5]) {
		str_format(buf, sizeof(buf), "- ** god kills: %d", ct->multis[5]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	}	
}

struct tee_stats tstats::read_statsfile (const char *name, time_t create)
{
	char path[128];
	int src_fd;
	struct tee_stats ret;
	
	memset(&ret, 0, sizeof(ret));
	
	snprintf(path, sizeof(path), "%s/%s", stat_dir, name);
	if ((src_fd = open(path, O_RDWR, 0777)) < 0) {
		if (create) {
			fprintf(stderr, "creating file %d / %d totals\n", num_totals, max_totals);
			if ((src_fd = open(path, O_WRONLY|O_CREAT, 0777)) < 0) {
				fprintf(stderr, "error creating file %s\n", path);
				return ret;
			}
			ret.join_time = 0;//create;
			write(src_fd, &ret, sizeof(ret));
			
			total_stats[num_totals].join_time = 0;//create;
			total_names[num_totals] = strdup(name);
			
			if (++num_totals >= max_totals - 5) {
				max_totals += 256;
				total_stats = (struct tee_stats *)realloc(total_stats,
					max_totals * sizeof(struct tee_stats));
				total_names = (char **)realloc(total_names, 
					max_totals * sizeof(char *));
			}
		}
	} else {
		read(src_fd, &ret, sizeof(ret));
	}
	close(src_fd);
	
	//ret.join_time = 0;

	return ret;
}

double tstats::get_ping (struct tee_stats fstats, char *buf)
{
	return (double)fstats.avg_ping;
}
double tstats::get_wrong (struct tee_stats fstats, char *buf)
{
	return (double)fstats.kills_wrong;
}
double tstats::get_max_multi (struct tee_stats fstats, char *buf)
{
	return (double)fstats.max_multi;
}
double tstats::get_max_spree (struct tee_stats fstats, char *buf)
{
	return (double)fstats.spree_max;
}
double tstats::get_steals (struct tee_stats fstats, char *buf)
{
	/*int k = fstats.kills + fstats.kills_x2 + fstats.kills_wrong;
	if (k && buf)
		sprintf(buf, "%.02f%%", 
			((double)fstats.steals / (double)k) * 100);
	*/	
	return (double)(fstats.steals - fstats.stolen_from);
}
double tstats::get_neg_steals (struct tee_stats fstats, char *buf)
{
	return (double)(fstats.stolen_from - fstats.steals);
}
double tstats::get_kd (struct tee_stats fstats, char *buf)
{
	int k = fstats.kills + fstats.kills_x2 + fstats.kills_wrong;
	int d = fstats.deaths ? fstats.deaths : 1;
	return (double)k / (double)d;
}
double tstats::get_kd_all (struct tee_stats fstats, char *buf)
{
	int k = fstats.kills + fstats.kills_x2 + fstats.kills_wrong;
	if (k < 1000)
		return 0.0f;
	int d = fstats.deaths ? fstats.deaths : 1;
	return (double)k / (double)d;
}
double tstats::get_kills (struct tee_stats fstats, char *buf)
{
	return (double)(fstats.kills + fstats.kills_x2 + fstats.kills_wrong);
}
double tstats::get_accuracy (struct tee_stats fstats, char *buf)
{
	if (fstats.shots < 10)
		return 0.0f;
		
	if (buf)
		sprintf(buf, "%d ping", fstats.avg_ping);
		
	int d = fstats.shots ? fstats.shots : 1;
	return 100 * (double)fstats.freezes / (double)d;
}
double tstats::get_accuracy_all (struct tee_stats fstats, char *buf)
{
	if (fstats.shots < 2000)
		return 0.0f;
		
	if (buf)
		sprintf(buf, "%d ping", fstats.avg_ping);
		
	int d = fstats.shots ? fstats.shots : 1;
	return 100 * (double)fstats.freezes / (double)d;
}
double tstats::get_bounces (struct tee_stats fstats, char *buf)
{
	return (double)fstats.bounce_shots;
}
double tstats::get_hammers (struct tee_stats fstats, char *buf)
{
	return (double)fstats.hammers;
}
double tstats::get_suicides (struct tee_stats fstats, char *buf)
{
	return (double)fstats.suicides;
}

void tstats::update_stats (struct tee_stats *dst, struct tee_stats *src)
{
	if (!dst || !src) {
		printf("[%s]: dst = %p, src = %p\n", __func__, dst, src);
		return;
	}
		
	if (!dst->join_time)
		dst->join_time = time(NULL);
	
	if (src->spree_max > dst->spree_max)
		dst->spree_max = src->spree_max;
	if (src->max_multi > dst->max_multi)
		dst->max_multi = src->max_multi;
	
	for (int i = 0; i < 6; i++)
		dst->multis[i] += src->multis[i];
		
	printf("[%s]: kills = %d += %d\n", __func__, dst->kills, src->kills);
		
	dst->version = src->version;
	dst->id = src->id;
	dst->kills += src->kills;
	dst->kills_x2 += src->kills_x2;
	dst->kills_wrong += src->kills_wrong;
	dst->deaths += src->deaths;
	dst->steals += src->steals;
	dst->suicides += src->suicides;
	dst->shots += src->shots;
	dst->freezes += src->freezes;
	dst->frozen += src->frozen;
	dst->hammers += src->hammers;
	dst->hammered += src->hammered;
	dst->bounce_shots += src->bounce_shots;
	dst->stolen_from += src->stolen_from;
	if (src->is_bot)
		dst->is_bot += 1;
	dst->join_time += (time(NULL) - src->join_time);
	dst->avg_ping = (unsigned short)ADD_AVG(src->avg_ping, 
						dst->avg_ping, 
						dst->num_samples);
			//(unsigned short)((float)(src->avg_ping + 
			//		(float)(dst->num_samples * 
			//		dst->avg_ping)) / 
			//		(++dst->num_samples));
}

void tstats::merge_into (const char *src, const char *dst)
{
	struct tee_stats *srcs, *dsts;
	char pb[128];
	int src_fd;
	
	printf("merge %s -> %s\n", src, dst);
	
	if (!(srcs = find_total_entry(src)) || !(dsts = find_total_entry(dst))) {
		printf("couldnt retrieve stats\n");
		return;
	}
	
	snprintf(pb, sizeof(pb), "merge %s (%d shots) into %s (%d shots)", 
		src, srcs->shots, dst, dsts->shots);
	SendChat(-1, CGameContext::CHAT_ALL, pb);	

	update_stats(dsts, srcs);			
						
	snprintf(pb, sizeof(pb), "%s/%s", stat_dir, dst);
	if ((src_fd = open(pb, O_RDWR, 0777)) < 0) {
		fprintf(stderr, "creating file\n");
		if ((src_fd = open(pb, O_WRONLY|O_CREAT, 0777)) < 0) {
			fprintf(stderr, "error creating %s\n", pb);
			return;	
		}
	}
	if (write(src_fd, dsts, sizeof(struct tee_stats)) != sizeof(struct tee_stats))
		printf("write failed\n");
	close(src_fd);
	
	memset(srcs, 0, sizeof(struct tee_stats));
	snprintf(pb, sizeof(pb), "%s/%s", stat_dir, src);
	remove(pb);
	
	snprintf(pb, sizeof(pb), "merged %s (%d shots) into %s (%d shots)", 
		src, srcs->shots, dst, dsts->shots);
	SendChat(-1, CGameContext::CHAT_ALL, pb);
}

void tstats::on_round_end (void)
{
	int i, j, src_fd, len;
	struct tee_stats totals;
	char path[128];
	
	print_best("most net steals:", 2, &get_steals, 0);
	print_best("best spree:", 1, &get_max_spree, 0);
	print_best("best multi:", 1, &get_max_multi, 0);
	print_best("best k/d:", 2, &get_kd, 0);	
	print_best("most wallshots:", 2, &get_bounces, 0);
	print_best("most kills:", 3, &get_kills, 0);
	print_best("best accuracy:", 4, &get_accuracy, 0);
	for (i = 0; i < round_index; i++) {
		if (!round_names[i][0]) {
			printf("[%s]: no entry found at %d\n", __func__, i);
			continue;
		}
		len = (int)strlen(round_names[i]);
		for (j = 0; j < num_totals; j++) {
			if ((strlen(total_names[j]) == len) && 
			    !memcmp(round_names[i], total_names[j], len))
				break;
		}
		printf("search for %s found at %d (%s)\n", round_names[i], 
			j, total_names[j]);
		if (j == num_totals)
			total_stats[j] = read_statsfile(round_names[i], time(NULL));

		update_stats(&total_stats[j], &round_stats[i]);			
						
		snprintf(path, sizeof(path), "%s/%s", stat_dir, round_names[i]);
		if ((src_fd = open(path, O_RDWR, 0777)) < 0) {
			fprintf(stderr, "creating file\n");
			if ((src_fd = open(path, O_WRONLY|O_CREAT, 0777)) < 0) {
				fprintf(stderr, "error creating %s\n", path);
				perror("a");
				continue;	
			}
		}
	
		write(src_fd, &total_stats[j], sizeof(struct tee_stats));
		close(src_fd);
	}
	memset(round_stats, 0, sizeof(round_stats));
	for (i = 0; i < 512; i++)
		memset(round_names[i], 0, 64);
	round_index = 0;
	printf("round ended !\n");
	
	for (i = 0; i < MAX_CLIENTS; i++) {
		int version, id;
		if (!game_server->m_apPlayers[i])
			continue;
		id = game_server->m_apPlayers[i]->GetCID();
		
		memset(&totals, 0, sizeof(totals));
		totals = read_statsfile(ID_NAME(id), 0);
		version = totals.version;
		memset(&totals, 0, sizeof(totals));
		totals.join_time = time(NULL);
		totals.id = id;
		totals.version = version;
		add_round_entry(totals, ID_NAME(id));
		printf("re-added player %d %s v %d\n", id, ID_NAME(id), version);
	}
}

struct tee_stats *tstats::find_round_entry (const char *name)
{
	int i, len = (int)strlen(name);
	
	for (i = 0; i < 512; i++)
		if (strlen(round_names[i]) == len && !memcmp(name, round_names[i], len))
			return &round_stats[i];
			
	return NULL;
}

struct tee_stats *tstats::find_total_entry (const char *name)
{
	int j, len = (int)strlen(name);
	
	for (j = 0; j < num_totals; j++) {
		if (strlen(total_names[j]) == len && !memcmp(name, total_names[j], len)) {
			printf("search %s found at %d (%s)\n", name, j, total_names[j]);
			return &total_stats[j];
		}
	}
	
	printf("[%s]: player %s not found\n", __func__, name);
	return NULL;	
}

struct tee_stats *tstats::add_round_entry (struct tee_stats st, const char *name)
{
	int i;
	
	for (i = 0; i < 512; i++)
		if (!strncmp(name, round_names[i], strlen(name)))
			break;
	if (i == 512)
		i = round_index++;
	if (i >= 512) {
		printf("exceeded max round player entries!\n");
		return NULL;
	}
	
	printf("adding round entry for %s (%d) id %d\n", name, i, st.id);
	
	strcpy(round_names[i], name);
	
	update_stats(&round_stats[i], &st);
	
	current[st.id] = &round_stats[i];
			
	return &round_stats[i];
}
#define ID_ENTRY(i) (game_server->m_pController->t_stats->current[i])

void tstats::do_kill_messages (struct tee_stats *s_killer, struct tee_stats *s_victim)
{	
	if (!s_killer || !s_victim)
		return;
	const char *kname = Server()->ClientName(s_killer->id);
	int Victim = s_victim->id;
	s_victim->deaths++;
	if (s_victim->frozeby != s_killer->id && s_victim->frozeby >= 0) {
		struct tee_stats *s_owner = ID_ENTRY(s_victim->frozeby);
		if (s_owner) {
			s_killer->steals++;
			s_owner->stolen_from++;
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "'%s' stole %s's kill!", 
				kname, Server()->ClientName(s_victim->frozeby));
			game_server->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
		} else {
			printf("no owner found with id %d\n", s_victim->frozeby);
		}
	}

	/* handle spree */
	if (((++s_killer->spree) % 5) == 0) {
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "'%s' is on a spree of %d kills!", 
			kname, s_killer->spree);
		game_server->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}
	if (s_victim->spree >= 5) {
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s's spree of %d kills ended by '%s'!", 
			Server()->ClientName(Victim), s_victim->spree, kname);
		game_server->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}
	if (s_killer->spree > s_killer->spree_max)
		s_killer->spree_max = s_killer->spree;
	s_victim->spree = 0;

	/* handle multis */
	time_t ttmp = time(NULL);
	if ((ttmp - s_killer->lastkilltime) <= 5) {
		s_killer->multi++;
		if (s_killer->max_multi < s_killer->multi)
			s_killer->max_multi = s_killer->multi;
		int index = s_killer->multi - 2;
		s_killer->multis[index > 5 ? 5 : index]++;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "'%s' multi x%d!", 
			kname, s_killer->multi);
		game_server->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	} else {
		s_killer->multi = 1;
	}
	s_killer->lastkilltime = ttmp;	
	s_victim->frozeby = -1;	
}

void tstats::top_special (const char *message, int ClientID)
{
	if (strncmp(message, "/topping", 8) == 0) {
		print_best("highest ping:", 12, &get_ping, (message[8] == 'a'));
	} else if (strncmp(message, "/topwrong", 9) == 0) {
		print_best("most wrong-shrine kills:", 12, &get_wrong, (message[9] == 'a'));
	} else if (strncmp(message, "/topkills", 9) == 0) {
		print_best("most kills:", 12, &get_kills, (message[9] == 'a'));
	} else if (strncmp(message, "/topsteals", 10) == 0) {
		print_best("most net steals:", 12, &get_steals, (message[10] == 'a'));
	} else if (strncmp(message, "/fewsteals", 10) == 0) {
		print_best("fewest net steals:", 12, &get_neg_steals, (message[10] == 'a'));
	} else if (strncmp(message, "/topwalls", 9) == 0) {
		print_best("most wallshots:", 12, &get_bounces, (message[9] == 'a'));
	} else if (strncmp(message, "/topkd", 6) == 0) {
		if (message[6] == 'a')
			print_best("best kd (>1000 kills):", 20, &get_kd_all, 1);
		else
			print_best("best kd:", 12, &get_kd, 0);
	} else if (strncmp(message, "/topaccuracy", 12) == 0) {
		if (message[12] == 'a')
			print_best("best accuracy (>2000 shots):", 20, &get_accuracy_all, 1);
		else
			print_best("best accuracy:", 12, &get_accuracy, 0);
	} else if (strncmp(message, "/tophammers", 11) == 0) {
		print_best("most hammers:", 11, &get_hammers, 
			(message[11] == 'a'));
	} else if (strncmp(message, "/topsuicides", 12) == 0) {
		print_best("most suicides:", 12, &get_suicides, 
			(message[12] == 'a'));
	} else if (strncmp(message, "/topall", 7) == 0) {
		char mg[128] = { 0 };
		snprintf(mg, sizeof(mg), "all-time stats req by %s",
				 ID_NAME(ClientID));
		SendChat(-1, CGameContext::CHAT_ALL, mg);
		print_best("most steals:", 4, &get_steals, 1);
		print_best("most wallshots:", 4, &get_bounces, 1);
		print_best("best multi:", 2, &get_max_multi, 1);
		print_best("best spree:", 4, &get_max_spree, 1);
		print_best("most hammers:", 4, &get_hammers, 1);		
		print_best("most kills:", 4, &get_kills, 1);
	} else {
		print_best("most net steals:", 2, &get_steals, 0);
		print_best("best spree:", 1, &get_max_spree, 0);
		print_best("best multi:", 1, &get_max_multi, 0);
		print_best("best k/d:", 2, &get_kd, 0);	
		print_best("most wallshots:", 2, &get_bounces, 0);
		print_best("most kills:", 3, &get_kills, 0);
		print_best("best accuracy:", 4, &get_accuracy, 0);
	}
	last_reqd = (int)time(NULL);
}

void tstats::on_msg (const char *message, int ClientID)
{
	printf("[cmd msg] %s: %s\n", ID_NAME(ClientID), message);
	
	if (strncmp(message, "/statsall", 9) == 0) {
		int tl = (int)time(NULL) - last_reqds;
		if (tl < 5) { 
			char buf[64] = { 0 };
			snprintf(buf, sizeof(buf), "please wait %d seconds", 5 - tl);
			SendChatTarget(ClientID, buf);
		} else {
			struct tee_stats tmp;
			if (strlen(message) > 10) {
				char namebuf[64] = { 0 };
				strcpy(namebuf, message + 10);
				char *ptr = namebuf + strlen(namebuf) - 1;
				if (*ptr == ' ')
					*ptr = 0;			
				tmp = read_statsfile(namebuf, 0);
				if (!tmp.shots) {										
					SendChatTarget(ClientID, "invalid player");
					printf("invalid player %s\n", namebuf);
				} else {
					send_stats(namebuf, ClientID, &tmp, 1);
				}
			} else {
				tmp = read_statsfile(ID_NAME(ClientID), 0);
				send_stats(ID_NAME(ClientID), ClientID, &tmp, 1);
			}
			last_reqds = (int)time(NULL);
		}
	} else if (strncmp(message, "/stats", 6) == 0) {
		int tl = (int)time(NULL) - last_reqds;
		if (tl < 5) { 
			char buf[64] = { 0 };
			snprintf(buf, sizeof(buf), "please wait %d seconds", 5 - tl);
			SendChatTarget(ClientID, buf);
		} else {
			struct tee_stats *tmp;
			if (strlen(message) > 7) {
				char namebuf[64] = { 0 };
				strcpy(namebuf, message + 7);
				char *ptr = namebuf + strlen(namebuf) - 1;
				if (*ptr == ' ')
					*ptr = 0;
				if (!(tmp = find_round_entry(namebuf))) {
					SendChatTarget(ClientID, "invalid player");
					printf("invalid player %s\n", namebuf);
				} else {
					send_stats(namebuf, ClientID, tmp, 0);
				}
			} else {
				if ((tmp = current[ClientID]))
					send_stats(ID_NAME(ClientID), ClientID, tmp, 0);
			}
			last_reqds = (int)time(NULL);
		}
	} else if (strncmp(message, "/top", 3) == 0 || strncmp(message, "/few", 3) == 0) { 
		int tl = (int)time(NULL) - last_reqd;
		if (tl < 10) {
			char buf[64] = { 0 };
			snprintf(buf, sizeof(buf), "please wait %d seconds", 10 - tl);
			SendChatTarget(ClientID, buf);
		} else {
			top_special(message, ClientID);
		}
	} else if (strncmp(message, "/earrape", 8) == 0 && 
		   game_server->m_apPlayers[ClientID] && 
		   game_server->m_apPlayers[ClientID]->GetCharacter()) {
		if ((time(NULL) - ertimer) < (60 * 100)) {
			SendChatTarget(ClientID, "spammer");
		} else {
			for (int c = 0; c < 30; c++)
				game_server->CreateSound(game_server->m_apPlayers
					[ClientID]->GetCharacter()->m_Pos, SOUND_MENU);
			ertimer = time(NULL);
		}
	} else if (strncmp(message, "/crash", 6) == 0 && 
		   game_server->m_apPlayers[ClientID] && 
		   game_server->m_apPlayers[ClientID]->GetCharacter()) {
		game_server->m_apPlayers[ClientID]->GetCharacter()->force_weapon();
	} else if (strncmp(message, "/dump", 5) == 0) {
		int i, tl = (int)time(NULL) - last_reqd;
		if (tl > 5) {
			char abuf[128];
			for (i = 0; i < MAX_CLIENTS; i++) {
				memset(abuf, 0, sizeof(abuf));
				if (!game_server->m_apPlayers[i])
					continue;
				int tbn = game_server->m_apPlayers[i]->tb_num;
				if (tbn == 0)
					continue;
				float perc1 = ((float)game_server->m_apPlayers[i]->tb_under10 / 
					((float)tbn)); 
				float perc = ((float)game_server->m_apPlayers[i]->tb_under100k / 
					((float)tbn)); 
				snprintf(abuf, sizeof(abuf), 
					"** %s %d: %.02f%% 10 %.02f%% %d %d %d %d", 
					ID_NAME(game_server->m_apPlayers[i]->GetCID()), 
					tbn, perc1*100, perc*100, 
					game_server->m_apPlayers[i]->tbnum_10,
					game_server->m_apPlayers[i]->tbmax_10,
					game_server->m_apPlayers[i]->tbnum_44k,
					game_server->m_apPlayers[i]->tbmax_44k);	
				SendChat(-1, CGameContext::CHAT_ALL, abuf);	

			}
			last_reqd = (int)time(NULL);
		}
	}
}	
