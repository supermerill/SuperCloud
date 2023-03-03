#pragma once

#include <memory>
#include <chrono>

#include "network/ClusterManager.hpp"

namespace supercloud::test {

	class WaitConnection : public AbstractMessageManager, public std::enable_shared_from_this<WaitConnection> {
		std::shared_ptr<ClusterManager> m_mana;
		std::future<void> m_wait_for_connection;
		std::promise<void> m_connection_notifier;
		std::atomic_int connected = 0;
		size_t goal = 1;
		std::chrono::steady_clock::time_point m_start_time; //steady clock as we don't care of the real date,  only a good time
		WaitConnection() {
			m_wait_for_connection = m_connection_notifier.get_future();
			m_start_time = std::chrono::steady_clock::now();
		};
	public:
		//factory
		[[nodiscard]] static std::shared_ptr<WaitConnection> create(std::shared_ptr<ClusterManager> cluster_mana, size_t count = 1) {
			std::shared_ptr<WaitConnection> pointer = std::shared_ptr<WaitConnection>{ new WaitConnection() };
			pointer->m_mana = cluster_mana;
			pointer->goal = count;
			cluster_mana->registerListener(*UnnencryptedMessageType::NEW_CONNECTION, pointer);
			return pointer;
		}

		std::shared_ptr<WaitConnection> ptr() {
			return shared_from_this();
		}

		void receiveMessage(PeerPtr sender, uint8_t message_id, const ByteBuff& message) override {
			if (message_id == *UnnencryptedMessageType::NEW_CONNECTION) {
				if (++connected == goal) {
					m_connection_notifier.set_value();
				}
				if (connected >= goal) {
					m_mana->unregisterListener(message_id, ptr());
				}
			}
		}

		inline std::future<void>& getFuture() { return m_wait_for_connection; }

		WaitConnection& startWait() {
			m_start_time = std::chrono::steady_clock::now();
			return *this;
		}
		inline int64_t waitConnection(std::chrono::milliseconds max_wait) {
			std::chrono::time_point now_before = std::chrono::steady_clock::now();
			std::chrono::time_point until_time_point = m_start_time + max_wait;
			std::future_status wait_res = m_wait_for_connection.wait_until(until_time_point);
			std::chrono::time_point now = std::chrono::steady_clock::now();
			std::chrono::duration milis = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start_time);
			std::cout << "time since start and nowbefore: " << std::chrono::duration_cast<std::chrono::milliseconds>(now_before - m_start_time).count() << "\n";
			std::cout << "time since nowbefore and now: " << std::chrono::duration_cast<std::chrono::milliseconds>(now - now_before).count() << "\n";
			std::cout << "connection of  a peer in less than " << milis.count() 
				<< " (=" << (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() - std::chrono::duration_cast<std::chrono::milliseconds>(m_start_time.time_since_epoch()).count())<<")"
				<< " milis, between " << std::chrono::duration_cast<std::chrono::milliseconds>(m_start_time.time_since_epoch()).count()
				<<" and "<< std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() << "\n";
			return wait_res == std::future_status::ready ? std::max(int64_t(1),(int64_t)milis.count()) : -milis.count();
		}

	};

}