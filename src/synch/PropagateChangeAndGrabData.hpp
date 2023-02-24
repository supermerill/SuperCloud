package remi.distributedFS.fs.messages;

import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

import remi.distributedFS.datastruct.FsChunk;
import remi.distributedFS.fs.FileSystemManager;
import remi.distributedFS.util.ByteBuff;

public class PropagateChangeAndGrabData extends PropagateChange implements Runnable {

	Thread grabbingThread;
	java.util.concurrent.ConcurrentLinkedQueue<FsChunk> chunkToGrab = new ConcurrentLinkedQueue<>();
	Semaphore nbChunkToGrab = new Semaphore(0);

	FileSystemManager manager;

	public PropagateChangeAndGrabData(FileSystemManager manager) {
		super(manager);
		this.grabbingThread = new Thread(this);
		grabbingThread.start();
	}
	
//	@Override
//	public void visit(FsChunk chunk) {
//		//ask to get it in the grabbing thread
//		chunkToGrab.add(chunk);
//		nbChunkToGrab.release();
//	}
	
	@Override
	protected void getAChunk(FsChunk newCHunk) {
		super.getAChunk(newCHunk);
		//ask to get it in the grabbing thread
		chunkToGrab.add(newCHunk);
		nbChunkToGrab.release();
	}
	
	@Override
	public void run() {
		ByteBuff buffUseless = new ByteBuff().put((byte)0).flip();
		while(true) {
			try {
				//don't grab everything too quickly
				Thread.sleep(100);
				//wait something to grab
				nbChunkToGrab.tryAcquire(10, TimeUnit.MINUTES);
			} catch (InterruptedException e) {
				e.printStackTrace();
			}
			if(!chunkToGrab.isEmpty()) {
				FsChunk chunk = chunkToGrab.poll();
				if(chunk != null) {
					chunk.read(buffUseless, 0, 1);
				}
			}
		}
		
	}
	

}
