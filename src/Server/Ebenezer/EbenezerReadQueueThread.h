#ifndef SERVER_EBENEZER_EBENEZERREADQUEUETHREAD_H
#define SERVER_EBENEZER_EBENEZERREADQUEUETHREAD_H

#pragma once

#include <shared-server/ReadQueueThread.h>

namespace Ebenezer
{

class EbenezerReadQueueThread : public ReadQueueThread
{
public:
	EbenezerReadQueueThread();

protected:
	void process_packet(const char* buffer, int len) override;

	/// \brief Handles DB_CUSTOM_KNIGHT_CASH_SPEND_RESPONSE: finishes the user's purchase, or gives
	/// the Knight Cash back if the user has gone
	void RecvKnightCashSpend(int userSocketId, const char* buffer);
};

} // namespace Ebenezer

#endif // SERVER_EBENEZER_EBENEZERREADQUEUETHREAD_H
