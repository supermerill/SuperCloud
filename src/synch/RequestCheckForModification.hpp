package remi.distributedFS.fs.messages;

import java.util.List;

import it.unimi.dsi.fastutil.ints.Int2ObjectMap;
import remi.distributedFS.datastruct.FsDirectory;
import remi.distributedFS.datastruct.FsFile;
import remi.distributedFS.datastruct.FsObject;
import remi.distributedFS.fs.StandardManager;
import remi.distributedFS.log.Logs;
import remi.distributedFS.net.ClusterManager;
import remi.distributedFS.net.impl.Peer;
import remi.distributedFS.net.impl.Peer.PeerConnectionState;
import remi.distributedFS.net.impl.PhysicalServer;
import remi.distributedFS.util.ByteBuff;

/**
 * This class ask for the hash of the root directory.
 * If it's not the same, it will ask for the hash of file inside it.
 * When the content of somethign is really modified (more than 1 directory hash direpancy)
 * then it will ask ProgateChange to sync the directory.
 * 
 * @author remi
 *
 */
public class RequestCheckForModification extends AbstractFSMessageManager{


	Int2ObjectMap<FsDirectory> peerid2ProblematicPath;
	
	private StandardManager manager;

	public RequestCheckForModification(StandardManager standardManager) {
		this.manager = standardManager;
	}
	
	@Override
	public void receiveMessage(long senderId, byte messageId, ByteBuff message) {
		if(manager.getNet().getComputerId(senderId) <0){
			//error: not a estabished peer
			Logs.logManager.warning("Error, peer "+senderId%100+" ask us a chunk and he doens't have a computerid !");
			return;
		}
		if (messageId == GET_HASH) {
			Logs.logManager.info(this.manager.getComputerId()+"$ RECEIVE GET_HASH from "+senderId);
			ByteBuff buff = new ByteBuff();
			int nbPaths = message.getTrailInt();
			buff.putTrailInt(nbPaths);
			for(int i=0;i<nbPaths;i++) {
				String path = message.getUTF8();
				long timestamp = message.getLong();
	
				FsDirectory dir = FsDirectory.FsDirectoryMethods.getPathDir( manager.getRoot(), path);
				
				if(dir == null /*|| timestamp > dir.getModifyDate()*/) {
					//Error, please try to sync
					Logs.logManager.info(this.manager.getComputerId()+"$ UNKNOW DIR PATH? REQUEST: "+path);
					PhysicalServer net = (PhysicalServer) manager.getNet();
					ByteBuff buff2 = new ByteBuff();
					buff2.putUTF8(path);
					buff2.putLong(-1);
					buff2.flip();
					net.getPeer(senderId).writeMessage(GET_DIR, buff2);
				}
	
				buff.putUTF8(path);
				buff.putLong(dir==null? 0 : dir.getModifyDate());
			}
			nbPaths = message.getTrailInt();
			buff.putTrailInt(nbPaths);
			for(int i=0;i<nbPaths;i++) {
				String path = message.getUTF8();
				long timestamp = message.getLong();
				FsFile fic = FsDirectory.FsDirectoryMethods.getPathFile( manager.getRoot(), path);
				if(fic == null || timestamp > fic.getModifyDate()) {
					//Error, please try to sync
					Logs.logManager.info(this.manager.getComputerId()+"$ UNKNOW FIC PATH? REQUEST: "+path);
					PhysicalServer net = (PhysicalServer) manager.getNet();
					ByteBuff buff2 = new ByteBuff();
					buff2.putUTF8(path);
					buff2.putLong(-1);
					buff2.flip();
					net.getPeer(senderId).writeMessage(GET_FILE_DESCR, buff2);
				}
	
				buff.putUTF8(path);
				buff.putLong(fic==null? 0 : fic.getModifyDate());
			}
			buff.flip();
//			manager.getNet().writeMessage(senderId, SEND_MAX_FLOW, buff);
			PhysicalServer net = (PhysicalServer) manager.getNet();
			net.getPeer(senderId).writeMessage(SEND_HASH, buff);
		}
		if (messageId == SEND_HASH) {
			int nbPaths = message.getTrailInt();
			FsDirectory dirToCheck = null;
			FsObject oneObj = null;

			for(int i=0;i<nbPaths;i++) {
				String path = message.getUTF8();
				long timestamp = message.getLong();
				FsDirectory dir = FsDirectory.FsDirectoryMethods.getPathDir( manager.getRoot(), path);
				if(dir != null) oneObj = dir;
				if(dir == null || timestamp > dir.getModifyDate()) {
					Logs.logManager.info(this.manager.getComputerId()+"$ GET HASH : DIR "+dir.getParent().getPath()+" is not uptodate");
					if(dirToCheck == null)
						dirToCheck = dir;
					else {
						Logs.logManager.info(this.manager.getComputerId()+"$ GET HASH : UPDATE DIR "+dir.getParent().getPath());
						// update parent directory!
						PhysicalServer net = (PhysicalServer) manager.getNet();
						ByteBuff buff2 = new ByteBuff();
						buff2.putUTF8(dir.getParent().getPath());
						buff2.putLong(dir.getParent().getId());
						buff2.flip();
						net.getPeer(senderId).writeMessage(GET_DIR, buff2);
						return;
					}
				}
			}
			nbPaths = message.getTrailInt();
			for(int i=0;i<nbPaths;i++) {
				String path = message.getUTF8();
				long timestamp = message.getLong();
				FsFile fic = FsDirectory.FsDirectoryMethods.getPathFile( manager.getRoot(), path);
				if(fic != null) oneObj = fic;
				if(fic == null || timestamp > fic.getModifyDate()) {
					Logs.logManager.info(this.manager.getComputerId()+"$ GET HASH : UPDATE FIC PARENT "+fic.getParent().getPath());
					// update parent directory!
					PhysicalServer net = (PhysicalServer) manager.getNet();
					ByteBuff buff2 = new ByteBuff();
					buff2.putUTF8(fic.getParent().getPath());
					buff2.putLong(fic.getParent().getId());
					buff2.flip();
					net.getPeer(senderId).writeMessage(GET_FILE_DESCR, buff2);
					return;
				}
			}
			
			//check the content of the modify one
			if(dirToCheck != null) {
				try {
					Thread.sleep(200);
				} catch (InterruptedException e) {
				}
				Logs.logManager.info(this.manager.getComputerId()+"$ GET HASH : go deeper: "+dirToCheck.getPath());
				if(dirToCheck.getDirs().isEmpty() && dirToCheck.getFiles().isEmpty()) {
					Logs.logManager.info(this.manager.getComputerId()+"$ GET HASH : UPDATE EMPTY DIR "+dirToCheck.getPath());
					// update parent directory!
					PhysicalServer net = (PhysicalServer) manager.getNet();
					ByteBuff buff2 = new ByteBuff();
					buff2.putUTF8(dirToCheck.getPath());
					buff2.putLong(dirToCheck.getId());
					buff2.flip();
					net.getPeer(senderId).writeMessage(GET_DIR, buff2);
					
					return;
				}else
					emitGetHash(dirToCheck, senderId);
			}else {
				if(oneObj != null && oneObj.getParent().getParent() != oneObj.getParent()) {
					//change in the dir data, not in child
					Logs.logManager.info(this.manager.getComputerId()+"$ GET HASH : UPDATE PARENT "+oneObj.getParent().getPath());
					// update parent directory!
					PhysicalServer net = (PhysicalServer) manager.getNet();
					ByteBuff buff2 = new ByteBuff();
					buff2.putUTF8(oneObj.getParent().getPath());
					buff2.putLong(oneObj.getParent().getId());
					buff2.flip();
					net.getPeer(senderId).writeMessage(GET_DIR, buff2);
					return;
				}else
					Logs.logManager.info(this.manager.getComputerId()+"$ GET HASH : ALL GREEN");
			}
		}
	}
	
	public void emitGetHash(FsDirectory dirToCheck, long peerId) {
		
		ByteBuff buff = new ByteBuff();
		List<FsDirectory> dirs = dirToCheck.getDirs();
		buff.putTrailInt(dirs.size());
		for(FsDirectory dir : dirs) {
			buff.putUTF8(dir.getPath());
			buff.putLong(dir.getModifyDate());
		}
		List<FsFile> files = dirToCheck.getFiles();
		buff.putTrailInt(files.size());
		for(FsFile fic : files) {
			buff.putUTF8(fic.getPath());
			buff.putLong(fic.getModifyDate());
		}
		buff.flip();
		PhysicalServer net = (PhysicalServer) manager.getNet();
		net.getPeer(peerId).writeMessage(GET_HASH, buff);
	}

	
	
	public void startRefreshThread(int interval) {
		refreshThread = new RefreshThread(interval);
		refreshThread.start();
	}

	RefreshThread refreshThread = null;
	public class RefreshThread extends Thread{
		private int interval;

		public RefreshThread(int interval) {
			this.interval = interval;
		}

		public void run() {
			while(refreshThread == this) {
				try {
					Thread.sleep(interval);
				} catch (InterruptedException e) {
				}
				refreshAll();
			}
		}
	}
	
	public void refreshAll() {
		PhysicalServer net = (PhysicalServer) manager.getNet();
		for(Peer p : net.getPeers()) {
			if(p.hasState(PeerConnectionState.CONNECTED_W_AES) && p.isAlive()) {
				try {
					Thread.sleep(100);
				} catch (InterruptedException e) {
				}
				emitGetHash(manager.getRoot(), p.getPeerId());
			}
		}
	}

	public void register(ClusterManager net) {
		net.registerListener(GET_HASH, this);
		net.registerListener(SEND_HASH, this);
	}
}
