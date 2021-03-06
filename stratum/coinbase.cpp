#include "stratum.h"

#define TX_VALUE(v, s)	((unsigned int)(v>>s)&0xff)

static void encode_tx_value(char *encoded, json_int_t value)
{
	sprintf(encoded, "%02x%02x%02x%02x%02x%02x%02x%02x",
		TX_VALUE(value, 0), TX_VALUE(value, 8), TX_VALUE(value, 16), TX_VALUE(value, 24),
		TX_VALUE(value, 32), TX_VALUE(value, 40), TX_VALUE(value, 48), TX_VALUE(value, 56));
}

static void job_pack_tx(YAAMP_COIND *coind, char *data, json_int_t amount, char *key)
{
	int ol = strlen(data);
	char evalue[32];
	encode_tx_value(evalue, amount);

	sprintf(data+strlen(data), "%s", evalue);

	if(coind->pos && !key)
		sprintf(data+strlen(data), "2321%sac", coind->pubkey);

	else
		sprintf(data+strlen(data), "1976a914%s88ac", key? key: coind->script_pubkey);

//	debuglog("pack tx %s\n", data+ol);
//	debuglog("pack tx %lld\n", amount);
}

void coinbase_aux(YAAMP_JOB_TEMPLATE *templ, char *aux_script)
{
	vector<string> hashlist = coind_aux_hashlist(templ->auxs, templ->auxs_size);
	while(hashlist.size() > 1)
	{
		vector<string> l;
		for(int i = 0; i < hashlist.size()/2; i++)
		{
			string s = hashlist[i*2] + hashlist[i*2+1];

			char bin[YAAMP_HASHLEN_BIN*2];
			char out[YAAMP_HASHLEN_STR];

			binlify((unsigned char *)bin, s.c_str());
			sha256_double_hash_hex(bin, out, YAAMP_HASHLEN_BIN*2);

			l.push_back(out);
		}

		hashlist = l;
	}

	char merkle_hash[4*1024];
	memset(merkle_hash, 0, 4*1024);
	string_be(hashlist[0].c_str(), merkle_hash);

	sprintf(aux_script+strlen(aux_script), "fabe6d6d%s%02x00000000000000", merkle_hash, templ->auxs_size);
//	debuglog("aux_script is %s\n", aux_script);
}

void coinbase_create(YAAMP_COIND *coind, YAAMP_JOB_TEMPLATE *templ, json_value *json_result)
{
	char eheight[32], etime[32];
	char entime[32] = { 0 };
	char commitment[128] = { 0 };

	ser_number(templ->height, eheight);
	ser_number(time(NULL), etime);
	if(coind->pos) ser_string_be(templ->ntime, entime, 1);

	char eversion1[32] = "01000000";
	if(coind->txmessage)
		strcpy(eversion1, "02000000");

	char script1[4*1024];
	sprintf(script1, "%s%s%s08", eheight, templ->flags, etime);

	char script2[32] = "7969696d7000"; // "yiimp\0" in hex ascii

	if(!coind->pos && !coind->isaux && templ->auxs_size)
		coinbase_aux(templ, script2);

	int script_len = strlen(script1)/2 + strlen(script2)/2 + 8;
	sprintf(templ->coinb1, "%s%s01"
		"0000000000000000000000000000000000000000000000000000000000000000"
		"ffffffff%02x%s", eversion1, entime, script_len, script1);

	sprintf(templ->coinb2, "%s00000000", script2);

	// segwit commitment, if needed
	if (templ->has_segwit_txs)
		sprintf(commitment, "0000000000000000%02x%s", (int) (strlen(coind->commitment)/2), coind->commitment);

	json_int_t available = templ->value;
	char payees[4];
	int npayees = 1;
	char script_dests[4096] = { 0 };
//
	json_value* founderreward = json_get_array(json_result, "founderreward");
	if(founderreward)
	{
		const char *payee = json_get_string(founderreward, "founderpayee");
		json_int_t amount = json_get_int(founderreward, "amount");
		if (payee && amount) 
		{			
			char script_payee[128] = { 0 };
			npayees++;
			available -= amount;
			base58_decode(payee, script_payee);
			job_pack_tx(coind, script_dests, amount, script_payee);
		}
	}

	json_value* superblock = json_get_array(json_result, "superblock");
	if(superblock)
	{
		for(int i = 0; i < superblock->u.array.length; i++) {
			const char *payee = json_get_string(superblock->u.array.values[i], "payee");
			json_int_t amount = json_get_int(superblock->u.array.values[i], "amount");
			if (payee && amount) {				
				char script_payee[128] = { 0 };
				npayees++;
				available -= amount;
				base58_decode(payee, script_payee);
				job_pack_tx(coind, script_dests, amount, script_payee);
			}
		}
	}

	json_value* masternode = json_get_object(json_result, "masternode");
	if(masternode)
	{
		const char *payee = json_get_string(masternode, "payee");
		json_int_t amount = json_get_int(masternode, "amount");
		if (payee && amount) {
			char script_payee[128] = { 0 };
			npayees++;
			available -= amount;
			base58_decode(payee, script_payee);
			job_pack_tx(coind, script_dests, amount, script_payee);
		}
	}

	sprintf(payees, "%02x", npayees);
	strcat(templ->coinb2, payees);
	strcat(templ->coinb2, script_dests);
	 job_pack_tx(coind, templ->coinb2, available, NULL);
	 strcat(templ->coinb2, "00000000"); // locktime
	 coind->reward = (double)available/100000000*coind->reward_mul;
}
