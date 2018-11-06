#ifndef __LINUX_ICENET_SKBUFF_H__
#define __LINUX_ICENET_SKBUFF_H__

struct sk_buff_cq_entry {
	struct sk_buff *skb;
};

struct sk_buff_cq {
	struct sk_buff_cq_entry entries[CONFIG_ICENET_RING_SIZE];
	int head;
	int tail;
};

#define SK_BUFF_CQ_COUNT(cq) CIRC_CNT(cq.head, cq.tail, CONFIG_ICENET_RING_SIZE)
#define SK_BUFF_CQ_SPACE(cq) CIRC_SPACE(cq.head, cq.tail, CONFIG_ICENET_RING_SIZE)

static inline void sk_buff_cq_init(struct sk_buff_cq *cq)
{
	cq->head = 0;
	cq->tail = 0;
}

static inline void sk_buff_cq_push(
		struct sk_buff_cq *cq, struct sk_buff *skb)
{
	cq->entries[cq->head].skb = skb;
	cq->head = (cq->head + 1) & (CONFIG_ICENET_RING_SIZE - 1);
}

static inline struct sk_buff *sk_buff_cq_pop(struct sk_buff_cq *cq)
{
	struct sk_buff *skb;

	skb = cq->entries[cq->tail].skb;
	cq->tail = (cq->tail + 1) & (CONFIG_ICENET_RING_SIZE - 1);

	return skb;
}

static inline int sk_buff_cq_tail_nsegments(struct sk_buff_cq *cq)
{
	struct sk_buff *skb;

	skb = cq->entries[cq->tail].skb;

	return skb_shinfo(skb)->nr_frags + 1;
}

#endif
